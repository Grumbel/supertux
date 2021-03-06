//  SuperTux -  A Jump'n Run
//  Copyright (C) 2004 Ingo Ruhnke <grumbel@gmail.com>
//  Copyright (C) 2006 Christoph Sommer <christoph.sommer@2006.expires.deltadevelopment.de>
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

#include "worldmap/tux.hpp"

#include <sstream>

#include "control/input_manager.hpp"
#include "editor/editor.hpp"
#include "sprite/sprite.hpp"
#include "sprite/sprite_manager.hpp"
#include "supertux/savegame.hpp"
#include "supertux/tile.hpp"
#include "util/log.hpp"
#include "worldmap/level_tile.hpp"
#include "worldmap/special_tile.hpp"
#include "worldmap/sprite_change.hpp"

namespace worldmap {

static const float TUXSPEED = 200;
static const float map_message_TIME = 2.8f;

Tux::Tux(WorldMap* worldmap) :
  m_back_direction(),
  m_worldmap(worldmap),
  m_sprite(SpriteManager::current()->create(m_worldmap->get_savegame().get_player_status().worldmap_sprite)),
  m_controller(InputManager::current()->get_controller()),
  m_input_direction(D_NONE),
  m_direction(D_NONE),
  m_tile_pos(),
  m_offset(0),
  m_moving(false),
  m_ghost_mode(false)
{
}

void
Tux::draw(DrawingContext& context)
{
  if (m_worldmap->is_panning()) return;

  std::string action = get_action_prefix_for_bonus(m_worldmap->get_savegame().get_player_status().bonus);
  if(!action.empty())
  {
    m_sprite->set_action(m_moving ? action + "-walking" : action + "-stop");
  }
  else
  {
    log_debug << "Bonus type not handled in worldmap." << std::endl;
    m_sprite->set_action("large-stop");
  }
  m_sprite->draw(context.color(), get_pos(), LAYER_OBJECTS);
}

std::string
Tux::get_action_prefix_for_bonus(const BonusType& bonus) const
{
  if(bonus == GROWUP_BONUS)
    return "large";
  if(bonus == FIRE_BONUS)
    return "fire";
  if(bonus == ICE_BONUS)
    return "ice";
  if(bonus == AIR_BONUS)
    return "air";
  if(bonus == EARTH_BONUS)
    return "earth";
  if(bonus == NO_BONUS)
    return "small";

  return "";
}

Vector
Tux::get_pos() const
{
  float x = m_tile_pos.x * 32;
  float y = m_tile_pos.y * 32;

  switch(m_direction)
  {
    case D_WEST:
      x -= m_offset - 32;
      break;
    case D_EAST:
      x += m_offset - 32;
      break;
    case D_NORTH:
      y -= m_offset - 32;
      break;
    case D_SOUTH:
      y += m_offset - 32;
      break;
    case D_NONE:
      break;
  }

  return Vector(x, y);
}

void
Tux::stop()
{
  m_offset = 0;
  m_direction = D_NONE;
  m_input_direction = D_NONE;
  m_moving = false;
}

void
Tux::set_direction(Direction dir)
{
  m_input_direction = dir;
}

void
Tux::set_ghost_mode(bool enabled)
{
  m_ghost_mode = enabled;
}

bool
Tux::get_ghost_mode() const
{
  return m_ghost_mode;
}

void
Tux::tryStartWalking()
{
  if (m_moving)
    return;
  if (m_input_direction == D_NONE)
    return;

  auto level = m_worldmap->at_level();

  // We got a new direction, so lets start walking when possible
  Vector next_tile;
  if ((!level || level->solved || level->perfect
      || (Editor::current() && Editor::current()->is_testing_level()))
      && m_worldmap->path_ok(m_input_direction, m_tile_pos, &next_tile)) {
    m_tile_pos = next_tile;
    m_moving = true;
    m_direction = m_input_direction;
    m_back_direction = reverse_dir(m_direction);
  } else if (m_ghost_mode || (m_input_direction == m_back_direction)) {
    m_moving = true;
    m_direction = m_input_direction;
    m_tile_pos = m_worldmap->get_next_tile(m_tile_pos, m_direction);
    m_back_direction = reverse_dir(m_direction);
  }
}

bool
Tux::canWalk(int tile_data, Direction dir) const
{
  return m_ghost_mode ||
    ((tile_data & Tile::WORLDMAP_NORTH && dir == D_NORTH) ||
     (tile_data & Tile::WORLDMAP_SOUTH && dir == D_SOUTH) ||
     (tile_data & Tile::WORLDMAP_EAST  && dir == D_EAST) ||
     (tile_data & Tile::WORLDMAP_WEST  && dir == D_WEST));
}

void
Tux::ChangeSprite(SpriteChange* sprite_change)
{
  //SpriteChange* sprite_change = m_worldmap->at_sprite_change(tile_pos);
  if(sprite_change != nullptr) {
    m_sprite = sprite_change->sprite->clone();
    sprite_change->clear_stay_action();
    m_worldmap->get_savegame().get_player_status().worldmap_sprite = sprite_change->sprite_name;
  }
}

void
Tux::tryContinueWalking(float dt_sec)
{
  if (!m_moving)
    return;

  // Let tux walk
  m_offset += TUXSPEED * dt_sec;

  // Do nothing if we have not yet reached the next tile
  if (m_offset <= 32)
    return;

  m_offset -= 32;

  auto sprite_change = m_worldmap->at_sprite_change(m_tile_pos);
  ChangeSprite(sprite_change);

  // if this is a special_tile with passive_message, display it
  auto special_tile = m_worldmap->at_special_tile();
  if(special_tile)
  {
    // direction and the apply_action_ are opposites, since they "see"
    // directions in a different way
    if((m_direction == D_NORTH && special_tile->apply_action_south) ||
       (m_direction == D_SOUTH && special_tile->apply_action_north) ||
       (m_direction == D_WEST && special_tile->apply_action_east) ||
       (m_direction == D_EAST && special_tile->apply_action_west))
    {
      process_special_tile(special_tile);
    }
  }

  // check if we are at a Teleporter
  auto teleporter = m_worldmap->at_teleporter(m_tile_pos);

  // stop if we reached a level, a WORLDMAP_STOP tile, a teleporter or a special tile without a passive_message
  if ((m_worldmap->at_level())
      || (m_worldmap->tile_data_at(m_tile_pos) & Tile::WORLDMAP_STOP)
      || (special_tile && !special_tile->passive_message
          && special_tile->script.empty())
      || (teleporter) || m_ghost_mode) {
    if(special_tile && !special_tile->map_message.empty()
       && !special_tile->passive_message)
      m_worldmap->m_passive_message_timer.start(0);
    stop();
    return;
  }

  // if user wants to change direction, try changing, else guess the direction in which to walk next
  const int tile_data = m_worldmap->tile_data_at(m_tile_pos);
  if ((m_direction != m_input_direction) && canWalk(tile_data, m_input_direction)) {
    m_direction = m_input_direction;
    m_back_direction = reverse_dir(m_direction);
  } else {
    Direction dir = D_NONE;
    if (tile_data & Tile::WORLDMAP_NORTH && m_back_direction != D_NORTH)
      dir = D_NORTH;
    else if (tile_data & Tile::WORLDMAP_SOUTH && m_back_direction != D_SOUTH)
      dir = D_SOUTH;
    else if (tile_data & Tile::WORLDMAP_EAST && m_back_direction != D_EAST)
      dir = D_EAST;
    else if (tile_data & Tile::WORLDMAP_WEST && m_back_direction != D_WEST)
      dir = D_WEST;

    if (dir == D_NONE) {
      // Should never be reached if tiledata is good
      log_warning << "Could not determine where to walk next" << std::endl;
      stop();
      return;
    }

    m_direction = dir;
    m_input_direction = m_direction;
    m_back_direction = reverse_dir(m_direction);
  }

  // Walk automatically to the next tile
  if(m_direction == D_NONE)
    return;

  Vector next_tile;
  if (!m_ghost_mode && !m_worldmap->path_ok(m_direction, m_tile_pos, &next_tile)) {
    log_debug << "Tilemap data is buggy" << std::endl;
    stop();
    return;
  }

  auto next_sprite = m_worldmap->at_sprite_change(next_tile);
  if(next_sprite != nullptr && next_sprite->change_on_touch) {
    ChangeSprite(next_sprite);
  }
  //SpriteChange* last_sprite = m_worldmap->at_sprite_change(tile_pos);
  if(sprite_change != nullptr && next_sprite != nullptr) {
    log_debug << "Old: " << m_tile_pos << " New: " << next_tile << std::endl;
    sprite_change->set_stay_action();
  }

  m_tile_pos = next_tile;
}

void
Tux::updateInputDirection()
{
  if (m_controller->hold(Controller::UP))
    m_input_direction = D_NORTH;
  else if (m_controller->hold(Controller::DOWN))
    m_input_direction = D_SOUTH;
  else if (m_controller->hold(Controller::LEFT))
    m_input_direction = D_WEST;
  else if (m_controller->hold(Controller::RIGHT))
    m_input_direction = D_EAST;
}

void
Tux::update(float dt_sec)
{
  if (m_worldmap->is_panning()) return;

  updateInputDirection();
  if (m_moving)
    tryContinueWalking(dt_sec);
  else
    tryStartWalking();
}

void
Tux::setup()
{
  // check if we already touch a SpriteChange object
  auto sprite_change = m_worldmap->at_sprite_change(m_tile_pos);
  ChangeSprite(sprite_change);
}

void
Tux::process_special_tile(SpecialTile* special_tile) {
  if (!special_tile) {
    return;
  }

  if(special_tile->passive_message) {
    m_worldmap->m_passive_message = special_tile->map_message;
    m_worldmap->m_passive_message_timer.start(map_message_TIME);
  } else if(!special_tile->script.empty()) {
    try {
      m_worldmap->run_script(special_tile->script, "specialtile");
    } catch(std::exception& e) {
      log_warning << "Couldn't execute special tile script: " << e.what()
                  << std::endl;
    }
  }
}

} // namespace WorldmapNS

/* EOF */
