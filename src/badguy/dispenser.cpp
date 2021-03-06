//  SuperTux
//  Copyright (C) 2006 Matthias Braun <matze@braunis.de>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "badguy/dispenser.hpp"

#include "audio/sound_manager.hpp"
#include "editor/editor.hpp"
#include "math/random.hpp"
#include "object/bullet.hpp"
#include "object/player.hpp"
#include "sprite/sprite.hpp"
#include "supertux/game_object_factory.hpp"
#include "supertux/sector.hpp"
#include "util/reader_mapping.hpp"

Dispenser::Dispenser(const ReaderMapping& reader) :
  BadGuy(reader, "images/creatures/dispenser/dispenser.sprite"),
  ExposedObject<Dispenser, scripting::Dispenser>(this),
  cycle(),
  badguys(),
  next_badguy(0),
  dispense_timer(),
  autotarget(false),
  swivel(false),
  broken(false),
  random(),
  type(),
  type_str(),
  limit_dispensed_badguys(),
  max_concurrent_badguys(),
  current_badguys()
{
  set_colgroup_active(COLGROUP_MOVING_STATIC);
  SoundManager::current()->preload("sounds/squish.wav");
  reader.get("cycle", cycle, 5.0f);
  if ( !reader.get("badguy", badguys)) badguys.clear();
  reader.get("random", random, false);
  std::string type_s = "dropper"; //default
  reader.get("type", type_s, "");
  try
  {
    type = dispenser_type_from_string(type_s);
  }
  catch(std::exception&)
  {
    if(!Editor::is_active())
    {
      if(type_s.empty()) {
        log_warning << "No dispenser type set, setting to dropper." << std::endl;
      }
      else {
        log_warning << "Unknown type of dispenser:" << type_s << ", setting to dropper." << std::endl;
      }
    }
    type = DT_DROPPER;
  }

  type_str = get_type_string();

  reader.get("limit-dispensed-badguys", limit_dispensed_badguys, false);
  reader.get("max-concurrent-badguys", max_concurrent_badguys, 0);

//  if (badguys.size() <= 0)
//    throw std::runtime_error("No badguys in dispenser.");

  switch (type) {
    case DT_DROPPER:
      m_sprite->set_action("dropper");
      break;
    case DT_ROCKETLAUNCHER:
      m_sprite->set_action(m_dir == LEFT ? "working-left" : "working-right");
      set_colgroup_active(COLGROUP_MOVING); //if this were COLGROUP_MOVING_STATIC MrRocket would explode on launch.

      if (m_start_dir == AUTO) {
        autotarget = true;
      }
      break;
    case DT_CANNON:
      m_sprite->set_action("working");
      break;
    case DT_POINT:
      m_sprite->set_action("invisible");
      set_colgroup_active(COLGROUP_DISABLED);
    default:
      break;
  }

  m_bbox.set_size(m_sprite->get_current_hitbox_width(), m_sprite->get_current_hitbox_height());
  m_countMe = false;
}

void
Dispenser::draw(DrawingContext& context) {
  if (type != DT_POINT || Editor::is_active()) {
    BadGuy::draw(context);
  }
}

void
Dispenser::activate()
{
  if( broken ){
    return;
  }
  if( autotarget && !swivel ){ // auto cannon sprite might be wrong
    auto player = get_nearest_player();
    if( player ){
      m_dir = (player->get_pos().x > get_pos().x) ? RIGHT : LEFT;
      m_sprite->set_action(m_dir == LEFT ? "working-left" : "working-right");
    }
  }
  dispense_timer.start(cycle, true);
  launch_badguy();
}

void
Dispenser::deactivate()
{
  dispense_timer.stop();
}

//TODO: Add launching velocity to certain badguys
bool
Dispenser::collision_squished(GameObject& object)
{
  //Cannon launching MrRocket can be broken by jumping on it
  //other dispensers are not that fragile.
  if (broken || type != DT_ROCKETLAUNCHER) {
    return false;
  }

  if (m_frozen) {
    unfreeze();
  }

  m_sprite->set_action(m_dir == LEFT ? "broken-left" : "broken-right");
  dispense_timer.start(0);
  set_colgroup_active(COLGROUP_MOVING_STATIC); // Tux can stand on broken cannon.
  auto player = dynamic_cast<Player*>(&object);
  if (player){
    player->bounce(*this);
  }
  SoundManager::current()->play("sounds/squish.wav", get_pos());
  broken = true;
  return true;
}

HitResponse
Dispenser::collision(GameObject& other, const CollisionHit& hit)
{
  auto player = dynamic_cast<Player*> (&other);
  if(player) {
    // hit from above?
    if (player->get_bbox().p2.y < (m_bbox.p1.y + 16)) {
      collision_squished(*player);
      return FORCE_MOVE;
    }
    if(m_frozen && type != DT_CANNON){
      unfreeze();
    }
    return FORCE_MOVE;
  }

  auto bullet = dynamic_cast<Bullet*> (&other);
  if(bullet){
    return collision_bullet(*bullet, hit);
  }

  return FORCE_MOVE;
}

void
Dispenser::active_update(float )
{
  if (dispense_timer.check()) {
    // auto always shoots in Tux's direction
    if( autotarget ){
      if( m_sprite->animation_done()) {
        m_sprite->set_action(m_dir == LEFT ? "working-left" : "working-right");
        swivel = false;
      }

      auto player = get_nearest_player();
      if( player && !swivel ){
        Direction targetdir = (player->get_pos().x > get_pos().x) ? RIGHT : LEFT;
        if( m_dir != targetdir ){ // no target: swivel cannon
          swivel = true;
          m_dir = targetdir;
          m_sprite->set_action(m_dir == LEFT ? "swivel-left" : "swivel-right", 1);
        } else { // tux in sight: shoot
          launch_badguy();
        }
      }
    } else {
      launch_badguy();
    }
  }
}

void
Dispenser::launch_badguy()
{
  if (badguys.empty()) return;
  if (m_frozen) return;
  if (limit_dispensed_badguys &&
      current_badguys >= max_concurrent_badguys)
      return;

  //FIXME: Does is_offscreen() work right here?
  if (!is_offscreen() && !Editor::is_active()) {
    Direction launchdir = m_dir;
    if( !autotarget && m_start_dir == AUTO ){
      Player* player = get_nearest_player();
      if( player ){
        launchdir = (player->get_pos().x > get_pos().x) ? RIGHT : LEFT;
      }
    }

    if (badguys.size() > 1) {
      if (random) {
        next_badguy = gameRandom.rand(static_cast<int>(badguys.size()));
      }
      else {
        next_badguy++;

        if (next_badguy >= badguys.size())
          next_badguy = 0;
      }
    }

    std::string badguy = badguys[next_badguy];

    if(badguy == "random") {
      log_warning << "random is outdated; use a list of badguys to select from." << std::endl;
      return;
    }
    if(badguy == "goldbomb") {
      log_warning << "goldbomb is not allowed to be dispensed" << std::endl;
      return;
    }

    try {
      /* Need to allocate the badguy first to figure out its bounding box. */
      auto game_object = GameObjectFactory::instance().create(badguy, get_pos(), launchdir);
      if (game_object == nullptr)
        throw std::runtime_error("Creating " + badguy + " object failed.");

      auto& bad_guy = dynamic_cast<BadGuy&>(*game_object);

      Rectf object_bbox = bad_guy.get_bbox();

      Vector spawnpoint;
      switch (type) {
        case DT_DROPPER:
          spawnpoint = get_anchor_pos (m_bbox, ANCHOR_BOTTOM);
          spawnpoint.x -= 0.5f * object_bbox.get_width();
          break;
        case DT_ROCKETLAUNCHER:
        case DT_CANNON:
          spawnpoint = get_pos(); /* top-left corner of the cannon */
          if (launchdir == LEFT)
            spawnpoint.x -= object_bbox.get_width() + 1;
          else
            spawnpoint.x += m_bbox.get_width() + 1;
          break;
        case DT_POINT:
          spawnpoint = m_bbox.p1;
        default:
          break;
      }

      /* Now we set the real spawn position */
      bad_guy.set_pos(spawnpoint);

      /* We don't want to count dispensed badguys in level stats */
      bad_guy.m_countMe = false;

      /* Set reference to dispenser in badguy itself */
      if(limit_dispensed_badguys)
      {
        bad_guy.set_parent_dispenser(this);
        current_badguys++;
      }

      Sector::get().add_object(std::move(game_object));
    } catch(const std::exception& e) {
      log_warning << "Error dispensing badguy: " << e.what() << std::endl;
      return;
    }
  }
}

void
Dispenser::freeze()
{
  if (broken) {
    return;
  }

  set_group(COLGROUP_MOVING_STATIC);
  m_frozen = true;

    if(type == DT_ROCKETLAUNCHER && m_sprite->has_action("iced-left"))
    // Only swivel dispensers can use their left/right iced actions.
    m_sprite->set_action(m_dir == LEFT ? "iced-left" : "iced-right", 1);
    // when the sprite doesn't have separate actions for left and right or isn't a rocketlauncher,
    // it tries to use an universal one.
  else
  {
    if(type == DT_CANNON && m_sprite->has_action("iced"))
      m_sprite->set_action("iced", 1);
      // When is the dispenser a cannon, it uses the "iced" action.
    else
    {
      if(m_sprite->has_action("dropper-iced"))
        m_sprite->set_action("dropper-iced", 1);
        // When is the dispenser a dropper, it uses the "dropper-iced".
      else
      {
        m_sprite->set_color(Color(0.6f, 0.72f, 0.88f));
        m_sprite->stop_animation();
        // When is the dispenser something else (unprobable), or has no matching iced sprite, it shades to blue.
      }
    }
  }
  dispense_timer.stop();
}

void
Dispenser::unfreeze()
{
  /*set_group(colgroup_active);
  frozen = false;

  sprite->set_color(Color(1.00, 1.00, 1.00f));*/
  BadGuy::unfreeze();

  set_correct_action();
  activate();
}

bool
Dispenser::is_freezable() const
{
  return true;
}

bool
Dispenser::is_flammable() const
{
  return false;
}

void
Dispenser::set_correct_action()
{
  switch (type) {
    case DT_DROPPER:
      m_sprite->set_action("dropper");
      break;
    case DT_ROCKETLAUNCHER:
      m_sprite->set_action(m_dir == LEFT ? "working-left" : "working-right");
      break;
    case DT_CANNON:
      m_sprite->set_action("working");
      break;
    case DT_POINT:
      m_sprite->set_action("invisible");
      break;
    default:
      break;
  }
}

ObjectSettings
Dispenser::get_settings()
{
  ObjectSettings result = BadGuy::get_settings();
  result.options.push_back( ObjectOption(MN_NUMFIELD, _("Interval (seconds)"), &cycle,
                                         "cycle"));
  result.options.push_back( ObjectOption(MN_TOGGLE, _("Random"), &random,
                                         "random"));
  result.options.push_back( ObjectOption(MN_BADGUYSELECT, _("Enemies"), &badguys,
                                         "badguy"));
  result.options.push_back(ObjectOption(MN_TOGGLE, _("Limit dispensed badguys"), &limit_dispensed_badguys,
                                        "limit-dispensed-badguys"));
  result.options.push_back(ObjectOption(MN_NUMFIELD, _("Max concurrent badguys"), &max_concurrent_badguys,
                                         "max-concurrent-badguys"));

  ObjectOption seq(MN_STRINGSELECT, _("Type"), &type);
  seq.select.push_back(_("dropper"));
  seq.select.push_back(_("rocket launcher"));
  seq.select.push_back(_("cannon"));
  seq.select.push_back(_("invisible"));

  result.options.push_back( seq );

  type_str = get_type_string();
  result.options.push_back( ObjectOption(MN_TEXTFIELD, "type", &type_str, "type", false));
  return result;
}

void
Dispenser::after_editor_set()
{
  BadGuy::after_editor_set();
  set_correct_action();
}

/* EOF */
