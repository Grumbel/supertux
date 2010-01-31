//  SuperTux -  A Jump'n Run
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

#include "supertux/sector.hpp"

#include <algorithm>
#include <math.h>

#include "audio/sound_manager.hpp"
#include "badguy/jumpy.hpp"
#include "lisp/list_iterator.hpp"
#include "math/aatriangle.hpp"
#include "object/background.hpp"
#include "object/bonus_block.hpp"
#include "object/brick.hpp"
#include "object/bullet.hpp"
#include "object/camera.hpp"
#include "object/cloud_particle_system.hpp"
#include "object/coin.hpp"
#include "object/comet_particle_system.hpp"
#include "object/display_effect.hpp"
#include "object/ghost_particle_system.hpp"
#include "object/gradient.hpp"
#include "object/invisible_block.hpp"
#include "object/particlesystem.hpp"
#include "object/particlesystem_interactive.hpp"
#include "object/player.hpp"
#include "object/portable.hpp"
#include "object/pulsing_light.hpp"
#include "object/rain_particle_system.hpp"
#include "object/smoke_cloud.hpp"
#include "object/snow_particle_system.hpp"
#include "object/text_object.hpp"
#include "object/tilemap.hpp"
#include "physfs/ifile_stream.hpp"
#include "scripting/squirrel_util.hpp"
#include "supertux/collision.hpp"
#include "supertux/constants.hpp"
#include "supertux/game_session.hpp"
#include "supertux/globals.hpp"
#include "supertux/level.hpp"
#include "supertux/object_factory.hpp"
#include "supertux/player_status.hpp"
#include "supertux/spawn_point.hpp"
#include "supertux/tile.hpp"
#include "trigger/sequence_trigger.hpp"
#include "util/file_system.hpp"

#define DEFORM_BOTTOM  AATriangle::DEFORM1
#define DEFORM_TOP     AATriangle::DEFORM2
#define DEFORM_LEFT    AATriangle::DEFORM3
#define DEFORM_RIGHT   AATriangle::DEFORM4

Sector* Sector::_current = 0;

bool Sector::show_collrects = false;
bool Sector::draw_solids_only = false;

Sector::Sector(Level* parent) :
  level(parent), 
  name(),
  bullets(),
  init_script(),
  gameobjects_new(),
  currentmusic(LEVEL_MUSIC),
  sector_table(),
  scripts(),
  ambient_light( 1.0f, 1.0f, 1.0f, 1.0f ), 
  gameobjects(),
  moving_objects(),
  spawnpoints(),
  portables(),
  music(),
  gravity(10.0), 
  player(0), 
  solid_tilemaps(),
  camera(0), 
  effect(0)
{
  add_object(new Player(GameSession::current()->get_player_status(), "Tux"));
  add_object(new DisplayEffect("Effect"));
  add_object(new TextObject("Text"));

  sound_manager->preload("sounds/shoot.wav");

  // create a new squirrel table for the sector
  using namespace scripting;

  sq_collectgarbage(global_vm);

  sq_newtable(global_vm);
  sq_pushroottable(global_vm);
  if(SQ_FAILED(sq_setdelegate(global_vm, -2)))
    throw scripting::SquirrelError(global_vm, "Couldn't set sector_table delegate");

  sq_resetobject(&sector_table);
  if(SQ_FAILED(sq_getstackobj(global_vm, -1, &sector_table)))
    throw scripting::SquirrelError(global_vm, "Couldn't get sector table");
  sq_addref(global_vm, &sector_table);
  sq_pop(global_vm, 1);
}

Sector::~Sector()
{
  using namespace scripting;

  deactivate();

  for(ScriptList::iterator i = scripts.begin();
      i != scripts.end(); ++i) {
    HSQOBJECT& object = *i;
    sq_release(global_vm, &object);
  }
  sq_release(global_vm, &sector_table);
  sq_collectgarbage(global_vm);

  update_game_objects();
  assert(gameobjects_new.size() == 0);

  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    GameObject* object = *i;
    before_object_remove(object);
    object->unref();
  }

  for(SpawnPoints::iterator i = spawnpoints.begin(); i != spawnpoints.end();
      ++i)
    delete *i;
}

Level*
Sector::get_level()
{
  return level;
}

GameObject*
Sector::parse_object(const std::string& name, const Reader& reader)
{
  if(name == "camera") {
    Camera* camera = new Camera(this, "Camera");
    camera->parse(reader);
    return camera;
  } else if(name == "particles-snow") {
    SnowParticleSystem* partsys = new SnowParticleSystem();
    partsys->parse(reader);
    return partsys;
  } else if(name == "particles-rain") {
    RainParticleSystem* partsys = new RainParticleSystem();
    partsys->parse(reader);
    return partsys;
  } else if(name == "particles-comets") {
    CometParticleSystem* partsys = new CometParticleSystem();
    partsys->parse(reader);
    return partsys;
  } else if(name == "particles-ghosts") {
    GhostParticleSystem* partsys = new GhostParticleSystem();
    partsys->parse(reader);
    return partsys;
  } else if(name == "particles-clouds") {
    CloudParticleSystem* partsys = new CloudParticleSystem();
    partsys->parse(reader);
    return partsys;
  } else if(name == "money") { // for compatibility with old maps
    return new Jumpy(reader);
  } else {
    try {
      return ObjectFactory::instance().create(name, reader);
    } catch(std::exception& e) {
      log_warning << e.what() << "" << std::endl;
      return 0;
    }
  }
}

void
Sector::parse(const Reader& sector)
{
  bool has_background = false;
  lisp::ListIterator iter(&sector);
  while(iter.next()) {
    const std::string& token = iter.item();
    if(token == "name") {
      iter.value()->get(name);
    } else if(token == "gravity") {
      iter.value()->get(gravity);
    } else if(token == "music") {
      iter.value()->get(music);
    } else if(token == "spawnpoint") {
      SpawnPoint* sp = new SpawnPoint(*iter.lisp());
      spawnpoints.push_back(sp);
    } else if(token == "init-script") {
      iter.value()->get(init_script);
    } else if(token == "ambient-light") {
      std::vector<float> vColor;
      sector.get( "ambient-light", vColor );
      if(vColor.size() < 3) {
        log_warning << "(ambient-light) requires a color as argument" << std::endl;
      } else {
        ambient_light = Color( vColor );
      }
    } else {
      GameObject* object = parse_object(token, *(iter.lisp()));
      if(object) {
        if(dynamic_cast<Background *>(object)) {
          has_background = true;
        } else if(dynamic_cast<Gradient *>(object)) {
          has_background = true;
        }
        add_object(object);
      }
    }
  }

  if(!has_background) {
    Gradient* gradient = new Gradient();
    gradient->set_gradient(Color(0.3, 0.4, 0.75), Color(1, 1, 1));
    add_object(gradient);
  }

  update_game_objects();

  if(solid_tilemaps.size() < 1) log_warning << "sector '" << name << "' does not contain a solid tile layer." << std::endl;

  fix_old_tiles();
  if(!camera) {
    log_warning << "sector '" << name << "' does not contain a camera." << std::endl;
    update_game_objects();
    add_object(new Camera(this, "Camera"));
  }

  update_game_objects();
}

void
Sector::parse_old_format(const Reader& reader)
{
  name = "main";
  reader.get("gravity", gravity);

  std::string backgroundimage;
  if (reader.get("background", backgroundimage) && (backgroundimage != "")) {
    if (backgroundimage == "arctis.png") backgroundimage = "arctis.jpg";
    if (backgroundimage == "arctis2.jpg") backgroundimage = "arctis.jpg";
    if (backgroundimage == "ocean.png") backgroundimage = "ocean.jpg";
    backgroundimage = "images/background/" + backgroundimage;
    if (!PHYSFS_exists(backgroundimage.c_str())) {
      log_warning << "Background image \"" << backgroundimage << "\" not found. Ignoring." << std::endl;
      backgroundimage = "";
    }
  }

  float bgspeed = .5;
  reader.get("bkgd_speed", bgspeed);
  bgspeed /= 100;

  Color bkgd_top, bkgd_bottom;
  int r = 0, g = 0, b = 128;
  reader.get("bkgd_red_top", r);
  reader.get("bkgd_green_top",  g);
  reader.get("bkgd_blue_top",  b);
  bkgd_top.red = static_cast<float> (r) / 255.0f;
  bkgd_top.green = static_cast<float> (g) / 255.0f;
  bkgd_top.blue = static_cast<float> (b) / 255.0f;

  reader.get("bkgd_red_bottom",  r);
  reader.get("bkgd_green_bottom", g);
  reader.get("bkgd_blue_bottom", b);
  bkgd_bottom.red = static_cast<float> (r) / 255.0f;
  bkgd_bottom.green = static_cast<float> (g) / 255.0f;
  bkgd_bottom.blue = static_cast<float> (b) / 255.0f;

  if(backgroundimage != "") {
    Background* background = new Background();
    background->set_image(backgroundimage, bgspeed);
    add_object(background);
  } else {
    Gradient* gradient = new Gradient();
    gradient->set_gradient(bkgd_top, bkgd_bottom);
    add_object(gradient);
  }

  std::string particlesystem;
  reader.get("particle_system", particlesystem);
  if(particlesystem == "clouds")
    add_object(new CloudParticleSystem());
  else if(particlesystem == "snow")
    add_object(new SnowParticleSystem());
  else if(particlesystem == "rain")
    add_object(new RainParticleSystem());

  Vector startpos(100, 170);
  reader.get("start_pos_x", startpos.x);
  reader.get("start_pos_y", startpos.y);

  SpawnPoint* spawn = new SpawnPoint;
  spawn->pos = startpos;
  spawn->name = "main";
  spawnpoints.push_back(spawn);

  music = "chipdisko.ogg";
  // skip reading music filename. It's all .ogg now, anyway
  /*
    reader.get("music", music);
  */
  music = "music/" + music;

  int width = 30, height = 15;
  reader.get("width", width);
  reader.get("height", height);

  std::vector<unsigned int> tiles;
  if(reader.get("interactive-tm", tiles)
     || reader.get("tilemap", tiles)) {
    TileMap* tilemap = new TileMap(level->get_tileset());
    tilemap->set(width, height, tiles, LAYER_TILES, true);

    // replace tile id 112 (old invisible tile) with 1311 (new invisible tile)
    for(size_t x=0; x < tilemap->get_width(); ++x) {
      for(size_t y=0; y < tilemap->get_height(); ++y) {
        uint32_t id = tilemap->get_tile_id(x, y);
        if(id == 112)
          tilemap->change(x, y, 1311);
      }
    }

    if (height < 19) tilemap->resize(width, 19);
    add_object(tilemap);
  }

  if(reader.get("background-tm", tiles)) {
    TileMap* tilemap = new TileMap(level->get_tileset());
    tilemap->set(width, height, tiles, LAYER_BACKGROUNDTILES, false);
    if (height < 19) tilemap->resize(width, 19);
    add_object(tilemap);
  }

  if(reader.get("foreground-tm", tiles)) {
    TileMap* tilemap = new TileMap(level->get_tileset());
    tilemap->set(width, height, tiles, LAYER_FOREGROUNDTILES, false);

    // fill additional space in foreground with tiles of ID 2035 (lightmap/black)
    if (height < 19) tilemap->resize(width, 19, 2035);

    add_object(tilemap);
  }

  // read reset-points (now spawn-points)
  const lisp::Lisp* resetpoints = reader.get_lisp("reset-points");
  if(resetpoints) {
    lisp::ListIterator iter(resetpoints);
    while(iter.next()) {
      if(iter.item() == "point") {
        Vector sp_pos;
        if(reader.get("x", sp_pos.x) && reader.get("y", sp_pos.y))
        {
          SpawnPoint* sp = new SpawnPoint;
          sp->name = "main";
          sp->pos = sp_pos;
          spawnpoints.push_back(sp);
        }
      } else {
        log_warning << "Unknown token '" << iter.item() << "' in reset-points." << std::endl;
      }
    }
  }

  // read objects
  const lisp::Lisp* objects = reader.get_lisp("objects");
  if(objects) {
    lisp::ListIterator iter(objects);
    while(iter.next()) {
      GameObject* object = parse_object(iter.item(), *(iter.lisp()));
      if(object) {
        add_object(object);
      } else {
        log_warning << "Unknown object '" << iter.item() << "' in level." << std::endl;
      }
    }
  }

  // add a camera
  Camera* camera = new Camera(this, "Camera");
  add_object(camera);

  update_game_objects();

  if(solid_tilemaps.size() < 1) log_warning << "sector '" << name << "' does not contain a solid tile layer." << std::endl;

  fix_old_tiles();
  update_game_objects();
}

void
Sector::fix_old_tiles()
{
  for(std::list<TileMap*>::iterator i = solid_tilemaps.begin(); i != solid_tilemaps.end(); i++) {
    TileMap* solids = *i;
    for(size_t x=0; x < solids->get_width(); ++x) {
      for(size_t y=0; y < solids->get_height(); ++y) {
        uint32_t    id   = solids->get_tile_id(x, y);
        const Tile *tile = solids->get_tile(x, y);
        Vector pos = solids->get_tile_position(x, y);

        if(id == 112) {
          add_object(new InvisibleBlock(pos));
          solids->change(x, y, 0);
        } else if(tile->getAttributes() & Tile::COIN) {
          add_object(new Coin(pos));
          solids->change(x, y, 0);
        } else if(tile->getAttributes() & Tile::FULLBOX) {
          add_object(new BonusBlock(pos, tile->getData()));
          solids->change(x, y, 0);
        } else if(tile->getAttributes() & Tile::BRICK) {
          add_object(new Brick(pos, tile->getData()));
          solids->change(x, y, 0);
        } else if(tile->getAttributes() & Tile::GOAL) {
          std::string sequence = tile->getData() == 0 ? "endsequence" : "stoptux";
          add_object(new SequenceTrigger(pos, sequence));
          solids->change(x, y, 0);
        }
      }
    }
  }

  // add lights for special tiles
  for(GameObjects::iterator i = gameobjects.begin(); i != gameobjects.end(); i++) {
    TileMap* tm = dynamic_cast<TileMap*>(*i);
    if (!tm) continue;
    for(size_t x=0; x < tm->get_width(); ++x) {
      for(size_t y=0; y < tm->get_height(); ++y) {
        uint32_t id = tm->get_tile_id(x, y);
        Vector pos = tm->get_tile_position(x, y);
        Vector center = pos + Vector(16, 16);

        // torch
        if (id == 1517) {
          float pseudo_rnd = (float)((int)pos.x % 10) / 10;
          add_object(new PulsingLight(center, 1.0f + pseudo_rnd, 0.9f, 1.0f, Color(1.0f, 1.0f, 0.6f, 1.0f)));
        }
        // lava or lavaflow
        if ((id == 173) || (id == 1700) || (id == 1705) || (id == 1706)) {
          // space lights a bit
          if ((((tm->get_tile_id(x-1, y)) != tm->get_tile_id(x,y))
               && (tm->get_tile_id(x, y-1) != tm->get_tile_id(x,y)))
              || ((x % 3 == 0) && (y % 3 == 0))) {
            float pseudo_rnd = (float)((int)pos.x % 10) / 10;
            add_object(new PulsingLight(center, 1.0f + pseudo_rnd, 0.8f, 1.0f, Color(1.0f, 0.3f, 0.0f, 1.0f)));
          }
        }

      }
    }
  }

}

HSQUIRRELVM
Sector::run_script(std::istream& in, const std::string& sourcename)
{
  using namespace scripting;

  // garbage collect thread list
  for(ScriptList::iterator i = scripts.begin();
      i != scripts.end(); ) {
    HSQOBJECT& object = *i;
    HSQUIRRELVM vm = object_to_vm(object);

    if(sq_getvmstate(vm) != SQ_VMSTATE_SUSPENDED) {
      sq_release(global_vm, &object);
      i = scripts.erase(i);
      continue;
    }

    ++i;
  }

  HSQOBJECT object = create_thread(global_vm);
  scripts.push_back(object);

  HSQUIRRELVM vm = object_to_vm(object);

  // set sector_table as roottable for the thread
  sq_pushobject(vm, sector_table);
  sq_setroottable(vm);

  try {
    compile_and_run(vm, in, "Sector " + name + " - " + sourcename);
  } catch(std::exception& e) {
    log_warning << "Error running script: " << e.what() << std::endl;
  }

  return vm;
}

void
Sector::add_object(GameObject* object)
{
  // make sure the object isn't already in the list
#ifndef NDEBUG
  for(GameObjects::iterator i = gameobjects.begin(); i != gameobjects.end();
      ++i) {
    if(*i == object) {
      assert("object already added to sector" == 0);
    }
  }
  for(GameObjects::iterator i = gameobjects_new.begin();
      i != gameobjects_new.end(); ++i) {
    if(*i == object) {
      assert("object already added to sector" == 0);
    }
  }
#endif

  object->ref();
  gameobjects_new.push_back(object);
}

void
Sector::activate(const std::string& spawnpoint)
{
  SpawnPoint* sp = 0;
  for(SpawnPoints::iterator i = spawnpoints.begin(); i != spawnpoints.end();
      ++i) {
    if((*i)->name == spawnpoint) {
      sp = *i;
      break;
    }
  }
  if(!sp) {
    log_warning << "Spawnpoint '" << spawnpoint << "' not found." << std::endl;
    if(spawnpoint != "main") {
      activate("main");
    } else {
      activate(Vector(0, 0));
    }
  } else {
    activate(sp->pos);
  }
}

void
Sector::activate(const Vector& player_pos)
{
  if(_current != this) {
    if(_current != NULL)
      _current->deactivate();
    _current = this;

    // register sectortable as sector in scripting
    HSQUIRRELVM vm = scripting::global_vm;
    sq_pushroottable(vm);
    sq_pushstring(vm, "sector", -1);
    sq_pushobject(vm, sector_table);
    if(SQ_FAILED(sq_createslot(vm, -3)))
      throw scripting::SquirrelError(vm, "Couldn't set sector in roottable");
    sq_pop(vm, 1);

    for(GameObjects::iterator i = gameobjects.begin();
        i != gameobjects.end(); ++i) {
      GameObject* object = *i;

      try_expose(object);
    }
  }
  try_expose_me();

  // spawn smalltux below spawnpoint
  if (!player->is_big()) {
    player->move(player_pos + Vector(0,32));
  } else {
    player->move(player_pos);
  }

  // spawning tux in the ground would kill him
  if(!is_free_of_tiles(player->get_bbox())) {
    log_warning << "Tried spawning Tux in solid matter. Compensating." << std::endl;
    Vector npos = player->get_bbox().p1;
    npos.y-=32;
    player->move(npos);
  }

  camera->reset(player->get_pos());
  update_game_objects();

  //Run default.nut just before init script
  //Check to see if it's in a levelset (info file)
  std::string basedir = FileSystem::dirname(get_level()->filename);
  if(PHYSFS_exists((basedir + "/info").c_str())) {
    try {
      IFileStream in(basedir + "/default.nut");
      run_script(in, "default.nut");
    } catch(std::exception& ) {
      // doesn't exist or erroneous; do nothing
    }
  }

  // Run init script
  if(init_script != "") {
    std::istringstream in(init_script);
    run_script(in, "init-script");
  }
}

void
Sector::deactivate()
{
  if(_current != this)
    return;

  // remove sector entry from global vm
  HSQUIRRELVM vm = scripting::global_vm;
  sq_pushroottable(vm);
  sq_pushstring(vm, "sector", -1);
  if(SQ_FAILED(sq_deleteslot(vm, -2, SQFalse)))
    throw scripting::SquirrelError(vm, "Couldn't unset sector in roottable");
  sq_pop(vm, 1);

  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    GameObject* object = *i;

    try_unexpose(object);
  }

  try_unexpose_me();
  _current = NULL;
}

Rectf
Sector::get_active_region()
{
  return Rectf(
    camera->get_translation() - Vector(1600, 1200),
    camera->get_translation() + Vector(1600, 1200) + Vector(SCREEN_WIDTH,SCREEN_HEIGHT));
}

void
Sector::update(float elapsed_time)
{
  player->check_bounds(camera);

  /* update objects */
  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    GameObject* object = *i;
    if(!object->is_valid())
      continue;

    object->update(elapsed_time);
  }

  /* Handle all possible collisions. */
  handle_collisions();
  update_game_objects();
}

void
Sector::update_game_objects()
{
  /** cleanup marked objects */
  for(std::vector<GameObject*>::iterator i = gameobjects.begin();
      i != gameobjects.end(); /* nothing */) {
    GameObject* object = *i;

    if(object->is_valid()) {
      ++i;
      continue;
    }

    before_object_remove(object);

    object->unref();
    i = gameobjects.erase(i);
  }

  /* add newly created objects */
  for(std::vector<GameObject*>::iterator i = gameobjects_new.begin();
      i != gameobjects_new.end(); ++i)
  {
    GameObject* object = *i;

    before_object_add(object);

    gameobjects.push_back(object);
  }
  gameobjects_new.clear();

  /* update solid_tilemaps list */
  //FIXME: this could be more efficient
  solid_tilemaps.clear();
  for(std::vector<GameObject*>::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i)
  {
    TileMap* tm = dynamic_cast<TileMap*>(*i);
    if (!tm) continue;
    if (tm->is_solid()) solid_tilemaps.push_back(tm);
  }

}

bool
Sector::before_object_add(GameObject* object)
{
  Bullet* bullet = dynamic_cast<Bullet*> (object);
  if(bullet != NULL) {
    bullets.push_back(bullet);
  }

  MovingObject* movingobject = dynamic_cast<MovingObject*> (object);
  if(movingobject != NULL) {
    moving_objects.push_back(movingobject);
  }

  Portable* portable = dynamic_cast<Portable*> (object);
  if(portable != NULL) {
    portables.push_back(portable);
  }

  TileMap* tilemap = dynamic_cast<TileMap*> (object);
  if(tilemap != NULL && tilemap->is_solid()) {
    solid_tilemaps.push_back(tilemap);
  }

  Camera* camera = dynamic_cast<Camera*> (object);
  if(camera != NULL) {
    if(this->camera != 0) {
      log_warning << "Multiple cameras added. Ignoring" << std::endl;
      return false;
    }
    this->camera = camera;
  }

  Player* player = dynamic_cast<Player*> (object);
  if(player != NULL) {
    if(this->player != 0) {
      log_warning << "Multiple players added. Ignoring" << std::endl;
      return false;
    }
    this->player = player;
  }

  DisplayEffect* effect = dynamic_cast<DisplayEffect*> (object);
  if(effect != NULL) {
    if(this->effect != 0) {
      log_warning << "Multiple DisplayEffects added. Ignoring" << std::endl;
      return false;
    }
    this->effect = effect;
  }

  if(_current == this) {
    try_expose(object);
  }

  return true;
}

void
Sector::try_expose(GameObject* object)
{
  ScriptInterface* object_ = dynamic_cast<ScriptInterface*> (object);
  if(object_ != NULL) {
    HSQUIRRELVM vm = scripting::global_vm;
    sq_pushobject(vm, sector_table);
    object_->expose(vm, -1);
    sq_pop(vm, 1);
  }
}

void
Sector::try_expose_me()
{
  HSQUIRRELVM vm = scripting::global_vm;
  sq_pushobject(vm, sector_table);
  scripting::SSector* this_ = static_cast<scripting::SSector*> (this);
  expose_object(vm, -1, this_, "settings", false);
  sq_pop(vm, 1);
}

void
Sector::before_object_remove(GameObject* object)
{
  Portable* portable = dynamic_cast<Portable*> (object);
  if(portable != NULL) {
    portables.erase(std::find(portables.begin(), portables.end(), portable));
  }
  Bullet* bullet = dynamic_cast<Bullet*> (object);
  if(bullet != NULL) {
    bullets.erase(std::find(bullets.begin(), bullets.end(), bullet));
  }
  MovingObject* moving_object = dynamic_cast<MovingObject*> (object);
  if(moving_object != NULL) {
    moving_objects.erase(
      std::find(moving_objects.begin(), moving_objects.end(), moving_object));
  }

  if(_current == this)
    try_unexpose(object);
}

void
Sector::try_unexpose(GameObject* object)
{
  ScriptInterface* object_ = dynamic_cast<ScriptInterface*> (object);
  if(object_ != NULL) {
    HSQUIRRELVM vm = scripting::global_vm;
    SQInteger oldtop = sq_gettop(vm);
    sq_pushobject(vm, sector_table);
    try {
      object_->unexpose(vm, -1);
    } catch(std::exception& e) {
      log_warning << "Couldn't unregister object: " << e.what() << std::endl;
    }
    sq_settop(vm, oldtop);
  }
}

void
Sector::try_unexpose_me()
{
  HSQUIRRELVM vm = scripting::global_vm;
  SQInteger oldtop = sq_gettop(vm);
  sq_pushobject(vm, sector_table);
  try {
    scripting::unexpose_object(vm, -1, "settings");
  } catch(std::exception& e) {
    log_warning << "Couldn't unregister object: " << e.what() << std::endl;
  }
  sq_settop(vm, oldtop);
}
void
Sector::draw(DrawingContext& context)
{
  context.set_ambient_color( ambient_light );
  context.push_transform();
  context.set_translation(camera->get_translation());

  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    GameObject* object = *i;
    if(!object->is_valid())
      continue;

    if (draw_solids_only)
    {
      TileMap* tm = dynamic_cast<TileMap*>(object);
      if (tm && !tm->is_solid())
        continue;
    }

    object->draw(context);
  }

  if(show_collrects) {
    Color col(0.2f, 0.2f, 0.2f, 0.7f);
    for(MovingObjects::iterator i = moving_objects.begin();
        i != moving_objects.end(); ++i) {
      MovingObject* object = *i;
      const Rectf& rect = object->get_bbox();

      context.draw_filled_rect(rect, col, LAYER_FOREGROUND1 + 10);
    }
  }

  context.pop_transform();
}

/*-------------------------------------------------------------------------
 * Collision Detection
 *-------------------------------------------------------------------------*/

/** r1 is supposed to be moving, r2 a solid object */
void check_collisions(collision::Constraints* constraints,
                      const Vector& movement, const Rectf& r1, const Rectf& r2,
                      GameObject* object = NULL, MovingObject* other = NULL, const Vector& addl_ground_movement = Vector(0,0))
{
  if(!collision::intersects(r1, r2))
    return;

  MovingObject *moving_object = dynamic_cast<MovingObject*> (object);
  CollisionHit dummy;
  if(other != NULL && !other->collides(*object, dummy))
    return;
  if(moving_object != NULL && !moving_object->collides(*other, dummy))
    return;

  // calculate intersection
  float itop    = r1.get_bottom() - r2.get_top();
  float ibottom = r2.get_bottom() - r1.get_top();
  float ileft   = r1.get_right() - r2.get_left();
  float iright  = r2.get_right() - r1.get_left();

  if(fabsf(movement.y) > fabsf(movement.x)) {
    if(ileft < SHIFT_DELTA) {
      constraints->min_right(r2.get_left());
      return;
    } else if(iright < SHIFT_DELTA) {
      constraints->max_left(r2.get_right());
      return;
    }
  } else {
    // shiftout bottom/top
    if(itop < SHIFT_DELTA) {
      constraints->min_bottom(r2.get_top());
      return;
    } else if(ibottom < SHIFT_DELTA) {
      constraints->max_top(r2.get_bottom());
      return;
    }
  }

  constraints->ground_movement += addl_ground_movement;
  if(other != NULL) {
    HitResponse response = other->collision(*object, dummy);
    if(response == ABORT_MOVE)
      return;

    if(other->get_movement() != Vector(0, 0)) {
      // TODO what todo when we collide with 2 moving objects?!?
      constraints->ground_movement = other->get_movement();
    }
  }

  float vert_penetration = std::min(itop, ibottom);
  float horiz_penetration = std::min(ileft, iright);
  if(vert_penetration < horiz_penetration) {
    if(itop < ibottom) {
      constraints->min_bottom(r2.get_top());
      constraints->hit.bottom = true;
    } else {
      constraints->max_top(r2.get_bottom());
      constraints->hit.top = true;
    }
  } else {
    if(ileft < iright) {
      constraints->min_right(r2.get_left());
      constraints->hit.right = true;
    } else {
      constraints->max_left(r2.get_right());
      constraints->hit.left = true;
    }
  }
}

/* Returns zero if a unisolid tile is non-solid due to the movement direction,
 * non-zero if the tile is solid due to direction. */
int check_movement_unisolid (const Vector& movement, const Tile* tile)
{
  int slope_info;
  double mv_x;
  double mv_y;
  double mv_tan;
  double slope_tan;

#define MV_NON_SOLID 0
#define MV_SOLID 1

  /* If the tile is not a slope, this is very easy. */
  if ((tile->getAttributes() & Tile::SLOPE) == 0)
  {
    if (movement.y >= 0) /* moving down */
      return MV_SOLID;
    else /* moving up */
      return MV_NON_SOLID;
  }

  /* Initialize mv_x and mv_y. Depending on the slope the axis are inverted so
   * that we can always use the "SOUTHEAST" case of the slope. The southeast
   * case is the following:
   *     .
   *    /!
   *   / !
   *  +--+
   */
  mv_x = (double) movement.x;
  mv_y = (double) movement.y;

  slope_info = tile->getData();
  switch (slope_info & AATriangle::DIRECTION_MASK)
  {
    case AATriangle::SOUTHEAST: /*    . */
      /* do nothing */          /*   /! */
      break;                    /*  / ! */
                                /* +--+ */
    case AATriangle::SOUTHWEST: /* .    */
      mv_x *= (-1.0);           /* !\   */
      break;                    /* ! \  */
                                /* +--+ */
    case AATriangle::NORTHEAST: /* +--+ */
      mv_y *= (-1.0);           /*  \ ! */
      break;                    /*   \! */
                                /*    ' */
    case AATriangle::NORTHWEST: /* +--+ */
      mv_x *= (-1.0);           /* ! /  */
      mv_y *= (-1.0);           /* !/   */
      break;                    /* '    */
  } /* switch (slope_info & DIRECTION_MASK) */

  /* Handle the easy cases first */
  /* If we're moving to the right and down, then the slope is solid. */
  if ((mv_x >= 0.0) && (mv_y >= 0.0)) /* 4th quadrant */
    return MV_SOLID;
  /* If we're moving to the left and up, then the slope is not solid. */
  else if ((mv_x <= 0.0) && (mv_y <= 0.0)) /* 2nd quadrant */
    return MV_NON_SOLID;

  /* The pure up-down and left-right movements have already been handled. */
  assert (mv_x != 0.0);
  assert (mv_y != 0.0);

  /* calculate tangent of movement */
  mv_tan = (-1.0) * mv_y / mv_x;

  /* determine tangent of the slope */
  slope_tan = 1.0;
  if (((slope_info & AATriangle::DEFORM_MASK) == DEFORM_BOTTOM)
      || ((slope_info & AATriangle::DEFORM_MASK) == DEFORM_TOP))
    slope_tan = 0.5; /* ~= 26.6 deg */
  else if (((slope_info & AATriangle::DEFORM_MASK) == DEFORM_LEFT)
      || ((slope_info & AATriangle::DEFORM_MASK) == DEFORM_RIGHT))
    slope_tan = 2.0; /* ~= 63.4 deg */

  /* up and right */
  if (mv_x > 0.0) /* 1st quadrant */
  {
    assert (mv_y < 0.0);
    if (mv_tan <= slope_tan)
      return MV_SOLID;
    else
      return MV_NON_SOLID;
  }
  /* down and left */
  else if (mv_x < 0.0) /* 3rd quadrant */
  {
    assert (mv_y > 0.0);
    if (mv_tan >= slope_tan)
      return MV_SOLID;
    else
      return MV_NON_SOLID;
  }

  assert (1 != 1);
  return (-1);

#undef MV_NON_SOLID
#undef MV_SOLID
} /* int check_movement_unisolid */

int is_above_line (float l_x, float l_y, float m,
    float p_x, float p_y)
{
  float interp_y = (l_y + (m * (p_x - l_x)));
  if (interp_y == p_y)
    return (1);
  else if (interp_y > p_y)
    return (1);
  else
    return (0);
}

int is_below_line (float l_x, float l_y, float m,
    float p_x, float p_y)
{
  if (is_above_line (l_x, l_y, m, p_x, p_y))
    return (0);
  else
    return (1);
}

int check_position_unisolid (const Rectf& obj_bbox,
    const Rectf& tile_bbox,
    const Tile* tile)
{
  int slope_info;
  float tile_x;
  float tile_y;
  float gradient;
  float delta_x;
  float delta_y;
  float obj_x;
  float obj_y;

#define POS_NON_SOLID 0
#define POS_SOLID 1

  /* If this is not a slope, this is - again - easy */
  if ((tile->getAttributes() & Tile::SLOPE) == 0)
  {
    if (obj_bbox.get_bottom () <= tile_bbox.get_top ())
      return POS_SOLID;
    else
      return POS_NON_SOLID;
  }

  /* There are 20 different cases. For each case, calculate a line that
   * describes the slope's surface. The line is defined by x, y, and m, the
   * gradient. */
  slope_info = tile->getData();
  switch (slope_info
      & (AATriangle::DIRECTION_MASK | AATriangle::DEFORM_MASK))
  {
    case AATriangle::SOUTHWEST:
    case AATriangle::SOUTHWEST | DEFORM_TOP:
    case AATriangle::SOUTHWEST | DEFORM_LEFT:
    case AATriangle::NORTHEAST:
    case AATriangle::NORTHEAST | DEFORM_TOP:
    case AATriangle::NORTHEAST | DEFORM_LEFT:
      tile_x = tile_bbox.get_left ();
      tile_y = tile_bbox.get_top ();
      gradient = 1.0;
      break;

    case AATriangle::SOUTHEAST:
    case AATriangle::SOUTHEAST | DEFORM_TOP:
    case AATriangle::SOUTHEAST | DEFORM_RIGHT:
    case AATriangle::NORTHWEST:
    case AATriangle::NORTHWEST | DEFORM_TOP:
    case AATriangle::NORTHWEST | DEFORM_RIGHT:
      tile_x = tile_bbox.get_right ();
      tile_y = tile_bbox.get_top ();
      gradient = -1.0;
      break;

    case AATriangle::SOUTHEAST | DEFORM_BOTTOM:
    case AATriangle::SOUTHEAST | DEFORM_LEFT:
    case AATriangle::NORTHWEST | DEFORM_BOTTOM:
    case AATriangle::NORTHWEST | DEFORM_LEFT:
      tile_x = tile_bbox.get_left ();
      tile_y = tile_bbox.get_bottom ();
      gradient = -1.0;
      break;

    case AATriangle::SOUTHWEST | DEFORM_BOTTOM:
    case AATriangle::SOUTHWEST | DEFORM_RIGHT:
    case AATriangle::NORTHEAST | DEFORM_BOTTOM:
    case AATriangle::NORTHEAST | DEFORM_RIGHT:
      tile_x = tile_bbox.get_right ();
      tile_y = tile_bbox.get_bottom ();
      gradient = 1.0;
      break;

    default:
      assert (23 == 42);
  }

  /* delta_x, delta_y: Gradient aware version of SHIFT_DELTA. Here, we set the
   * sign of the values only. Also, we determine here which corner of the
   * object's bounding box is the interesting one for us. */
  delta_x = 1.0 * SHIFT_DELTA;
  delta_y = 1.0 * SHIFT_DELTA;
  switch (slope_info & AATriangle::DIRECTION_MASK)
  {
    case AATriangle::SOUTHWEST:
      delta_x *= 1.0;
      delta_y *= -1.0;
      obj_x = obj_bbox.get_left ();
      obj_y = obj_bbox.get_bottom ();
      break;

    case AATriangle::SOUTHEAST:
      delta_x *= -1.0;
      delta_y *= -1.0;
      obj_x = obj_bbox.get_right ();
      obj_y = obj_bbox.get_bottom ();
      break;

    case AATriangle::NORTHWEST:
      delta_x *= 1.0;
      delta_y *= 1.0;
      obj_x = obj_bbox.get_left ();
      obj_y = obj_bbox.get_top ();
      break;

    case AATriangle::NORTHEAST:
      delta_x *= -1.0;
      delta_y *= 1.0;
      obj_x = obj_bbox.get_right ();
      obj_y = obj_bbox.get_top ();
      break;
  }

  /* Adapt the delta_x, delta_y and the gradient for the 26.6 deg and 63.4 deg
   * cases. */
  switch (slope_info & AATriangle::DEFORM_MASK)
  {
    case 0:
      delta_x *= .70710678118654752440; /* 1/sqrt(2) */
      delta_y *= .70710678118654752440; /* 1/sqrt(2) */
      break;

    case DEFORM_BOTTOM:
    case DEFORM_TOP:
      delta_x *= .44721359549995793928; /* 1/sqrt(5) */
      delta_y *= .89442719099991587856; /* 2/sqrt(5) */
      gradient *= 0.5;
      break;

    case DEFORM_LEFT:
    case DEFORM_RIGHT:
      delta_x *= .89442719099991587856; /* 2/sqrt(5) */
      delta_y *= .44721359549995793928; /* 1/sqrt(5) */
      gradient *= 2.0;
      break;
  }

  /* With a south slope, check if all points are above the line. If one point
   * isn't, the slope is not solid. => You can pass through a south-slope from
   * below but not from above. */
  if (((slope_info & AATriangle::DIRECTION_MASK) == AATriangle::SOUTHWEST)
      || ((slope_info & AATriangle::DIRECTION_MASK) == AATriangle::SOUTHEAST))
  {
    if (is_below_line (tile_x, tile_y, gradient, obj_x + delta_x, obj_y + delta_y))
      return (POS_NON_SOLID);
    else
      return (POS_SOLID);
  }
  /* northwest or northeast. Same as above, but inverted. You can pass from top
   * to bottom but not vice versa. */
  else
  {
    if (is_above_line (tile_x, tile_y, gradient, obj_x + delta_x, obj_y + delta_y))
      return (POS_NON_SOLID);
    else
      return (POS_SOLID);
  }

#undef POS_NON_SOLID
#undef POS_SOLID
} /* int check_position_unisolid */

void
Sector::collision_tilemap(collision::Constraints* constraints,
                          const Vector& movement, const Rectf& dest,
                          MovingObject& object) const
{
  // calculate rectangle where the object will move
  float x1 = dest.get_left();
  float x2 = dest.get_right();
  float y1 = dest.get_top();
  float y2 = dest.get_bottom();

  for(std::list<TileMap*>::const_iterator i = solid_tilemaps.begin(); i != solid_tilemaps.end(); i++) {
    TileMap* solids = *i;

    // test with all tiles in this rectangle
    Rect test_tiles = solids->get_tiles_overlapping(Rectf(x1, y1, x2, y2));

    for(int x = test_tiles.left; x < test_tiles.right; ++x) {
      for(int y = test_tiles.top; y < test_tiles.bottom; ++y) {
        const Tile* tile = solids->get_tile(x, y);
        if(!tile)
          continue;
        // skip non-solid tiles
        if((tile->getAttributes() & Tile::SOLID) == 0)
          continue;
        Rectf tile_bbox = solids->get_tile_bbox(x, y);

        // only handle unisolid when the player is falling down and when he was
        // above the tile before
        if(tile->getAttributes() & Tile::UNISOLID) {
          int status;

          /* Check if the tile is solid given the current movement. This works
           * for south-slopes (which are solid when moving "down") and
           * north-slopes (which are solid when moving "up". "up" and "down" is
           * in quotation marks because because the slope's gradient is taken
           * into account. This is more complex than just checking for (y > 0).
           * --octo */
          status = check_movement_unisolid (movement, tile);
          /* If zero is returned, the unisolid tile is non-solid. */
          if (status == 0)
            continue;

          /* Check whether the object is already *in* the tile. If so, the tile
           * is non-solid. Otherwise, if the object is "above" (south slopes)
           * or "below" (north slopes), the tile will be solid. */
          status = check_position_unisolid (object.get_bbox(), tile_bbox, tile);
          if (status == 0)
            continue;
        }

        if(tile->getAttributes() & Tile::SLOPE) { // slope tile
          AATriangle triangle;
          int slope_data = tile->getData();
          if (solids->get_drawing_effect() == VERTICAL_FLIP)
            slope_data = AATriangle::vertical_flip(slope_data);
          triangle = AATriangle(tile_bbox, slope_data);

          collision::rectangle_aatriangle(constraints, dest, triangle, solids->get_movement());
        } else { // normal rectangular tile
          check_collisions(constraints, movement, dest, tile_bbox, NULL, NULL, solids->get_movement());
        }
      }
    }
  }
}

uint32_t
Sector::collision_tile_attributes(const Rectf& dest) const
{
  float x1 = dest.p1.x;
  float y1 = dest.p1.y;
  float x2 = dest.p2.x;
  float y2 = dest.p2.y;

  uint32_t result = 0;
  for(std::list<TileMap*>::const_iterator i = solid_tilemaps.begin(); i != solid_tilemaps.end(); i++) {
    TileMap* solids = *i;

    // test with all tiles in this rectangle
    Rect test_tiles = solids->get_tiles_overlapping(Rectf(x1, y1, x2, y2));
    // For ice (only), add a little fudge to recognize tiles Tux is standing on.
    Rect test_tiles_ice = solids->get_tiles_overlapping(Rectf(x1, y1, x2, y2 + SHIFT_DELTA));

    for(int x = test_tiles.left; x < test_tiles.right; ++x) {
      int y;
      for(y = test_tiles.top; y < test_tiles.bottom; ++y) {
        const Tile* tile = solids->get_tile(x, y);
        if(!tile)
          continue;
        result |= tile->getAttributes();
      }
      for(; y < test_tiles_ice.bottom; ++y) {
        const Tile* tile = solids->get_tile(x, y);
        if(!tile)
          continue;
        result |= (tile->getAttributes() & Tile::ICE);
      }
    }
  }

  return result;
}

/** fills in CollisionHit and Normal vector of 2 intersecting rectangle */
static void get_hit_normal(const Rectf& r1, const Rectf& r2, CollisionHit& hit,
                           Vector& normal)
{
  float itop = r1.get_bottom() - r2.get_top();
  float ibottom = r2.get_bottom() - r1.get_top();
  float ileft = r1.get_right() - r2.get_left();
  float iright = r2.get_right() - r1.get_left();

  float vert_penetration = std::min(itop, ibottom);
  float horiz_penetration = std::min(ileft, iright);
  if(vert_penetration < horiz_penetration) {
    if(itop < ibottom) {
      hit.bottom = true;
      normal.y = vert_penetration;
    } else {
      hit.top = true;
      normal.y = -vert_penetration;
    }
  } else {
    if(ileft < iright) {
      hit.right = true;
      normal.x = horiz_penetration;
    } else {
      hit.left = true;
      normal.x = -horiz_penetration;
    }
  }
}

void
Sector::collision_object(MovingObject* object1, MovingObject* object2) const
{
  using namespace collision;

  const Rectf& r1 = object1->dest;
  const Rectf& r2 = object2->dest;

  CollisionHit hit;
  if(intersects(object1->dest, object2->dest)) {
    Vector normal;
    get_hit_normal(r1, r2, hit, normal);

    if(!object1->collides(*object2, hit))
      return;
    std::swap(hit.left, hit.right);
    std::swap(hit.top, hit.bottom);
    if(!object2->collides(*object1, hit))
      return;
    std::swap(hit.left, hit.right);
    std::swap(hit.top, hit.bottom);

    HitResponse response1 = object1->collision(*object2, hit);
    std::swap(hit.left, hit.right);
    std::swap(hit.top, hit.bottom);
    HitResponse response2 = object2->collision(*object1, hit);
    if(response1 == CONTINUE && response2 == CONTINUE) {
      normal *= (0.5 + DELTA);
      object1->dest.move(-normal);
      object2->dest.move(normal);
    } else if (response1 == CONTINUE && response2 == FORCE_MOVE) {
      normal *= (1 + DELTA);
      object1->dest.move(-normal);
    } else if (response1 == FORCE_MOVE && response2 == CONTINUE) {
      normal *= (1 + DELTA);
      object2->dest.move(normal);
    }
  }
}

void
Sector::collision_static(collision::Constraints* constraints,
                         const Vector& movement, const Rectf& dest,
                         MovingObject& object)
{
  collision_tilemap(constraints, movement, dest, object);

  // collision with other (static) objects
  for(MovingObjects::iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    MovingObject* moving_object = *i;
    if(moving_object->get_group() != COLGROUP_STATIC
       && moving_object->get_group() != COLGROUP_MOVING_STATIC)
      continue;
    if(!moving_object->is_valid())
      continue;

    if(moving_object != &object)
      check_collisions(constraints, movement, dest, moving_object->bbox,
                       &object, moving_object);
  }
}

void
Sector::collision_static_constrains(MovingObject& object)
{
  using namespace collision;
  float infinity = (std::numeric_limits<float>::has_infinity ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::max());

  Constraints constraints;
  Vector movement = object.get_movement();
  Rectf& dest = object.dest;
  float owidth = object.get_bbox().get_width();
  float oheight = object.get_bbox().get_height();

  for(int i = 0; i < 2; ++i) {
    collision_static(&constraints, Vector(0, movement.y), dest, object);
    if(!constraints.has_constraints())
      break;

    // apply calculated horizontal constraints
    if(constraints.bottom < infinity) {
      float height = constraints.bottom - constraints.top;
      if(height < oheight) {
        // we're crushed, but ignore this for now, we'll get this again
        // later if we're really crushed or things will solve itself when
        // looking at the vertical constraints
      }
      dest.p2.y = constraints.bottom - DELTA;
      dest.p1.y = dest.p2.y - oheight;
    } else if(constraints.top > -infinity) {
      dest.p1.y = constraints.top + DELTA;
      dest.p2.y = dest.p1.y + oheight;
    }
  }
  if(constraints.has_constraints()) {
    if(constraints.hit.bottom) {
      dest.move(constraints.ground_movement);
    }
    if(constraints.hit.top || constraints.hit.bottom) {
      constraints.hit.left = false;
      constraints.hit.right = false;
      object.collision_solid(constraints.hit);
    }
  }

  constraints = Constraints();
  for(int i = 0; i < 2; ++i) {
    collision_static(&constraints, movement, dest, object);
    if(!constraints.has_constraints())
      break;

    // apply calculated vertical constraints
    float width = constraints.right - constraints.left;
    if(width < infinity) {
      if(width + SHIFT_DELTA < owidth) {
#if 0
        printf("Object %p crushed horizontally... L:%f R:%f\n", &object,
               constraints.left, constraints.right);
#endif
        CollisionHit h;
        h.left = true;
        h.right = true;
        h.crush = true;
        object.collision_solid(h);
      } else {
        float xmid = (constraints.left + constraints.right) / 2;
        dest.p1.x = xmid - owidth/2;
        dest.p2.x = xmid + owidth/2;
      }
    } else if(constraints.right < infinity) {
      dest.p2.x = constraints.right - DELTA;
      dest.p1.x = dest.p2.x - owidth;
    } else if(constraints.left > -infinity) {
      dest.p1.x = constraints.left + DELTA;
      dest.p2.x = dest.p1.x + owidth;
    }
  }

  if(constraints.has_constraints()) {
    if( constraints.hit.left || constraints.hit.right
        || constraints.hit.top || constraints.hit.bottom
        || constraints.hit.crush )
      object.collision_solid(constraints.hit);
  }

  // an extra pass to make sure we're not crushed horizontally
  constraints = Constraints();
  collision_static(&constraints, movement, dest, object);
  if(constraints.bottom < infinity) {
    float height = constraints.bottom - constraints.top;
    if(height + SHIFT_DELTA < oheight) {
#if 0
      printf("Object %p crushed vertically...\n", &object);
#endif
      CollisionHit h;
      h.top = true;
      h.bottom = true;
      h.crush = true;
      object.collision_solid(h);
    }
  }
}

namespace {
const float MAX_SPEED = 16.0f;
}

void
Sector::handle_collisions()
{
  using namespace collision;

  // calculate destination positions of the objects
  for(MovingObjects::iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    MovingObject* moving_object = *i;
    Vector mov = moving_object->get_movement();

    // make sure movement is never faster than MAX_SPEED. Norm is pretty fat, so two addl. checks are done before.
    if (((mov.x > MAX_SPEED * M_SQRT1_2) || (mov.y > MAX_SPEED * M_SQRT1_2)) && (mov.norm() > MAX_SPEED)) {
      moving_object->movement = mov.unit() * MAX_SPEED;
      //log_debug << "Temporarily reduced object's speed of " << mov.norm() << " to " << moving_object->movement.norm() << "." << std::endl;
    }

    moving_object->dest = moving_object->get_bbox();
    moving_object->dest.move(moving_object->get_movement());
  }

  // part1: COLGROUP_MOVING vs COLGROUP_STATIC and tilemap
  for(MovingObjects::iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    MovingObject* moving_object = *i;
    if((moving_object->get_group() != COLGROUP_MOVING
        && moving_object->get_group() != COLGROUP_MOVING_STATIC
        && moving_object->get_group() != COLGROUP_MOVING_ONLY_STATIC)
       || !moving_object->is_valid())
      continue;

    collision_static_constrains(*moving_object);
  }

  // part2: COLGROUP_MOVING vs tile attributes
  for(MovingObjects::iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    MovingObject* moving_object = *i;
    if((moving_object->get_group() != COLGROUP_MOVING
        && moving_object->get_group() != COLGROUP_MOVING_STATIC
        && moving_object->get_group() != COLGROUP_MOVING_ONLY_STATIC)
       || !moving_object->is_valid())
      continue;

    uint32_t tile_attributes = collision_tile_attributes(moving_object->dest);
    if(tile_attributes >= Tile::FIRST_INTERESTING_FLAG) {
      moving_object->collision_tile(tile_attributes);
    }
  }

  // part2.5: COLGROUP_MOVING vs COLGROUP_TOUCHABLE
  for(MovingObjects::iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    MovingObject* moving_object = *i;
    if((moving_object->get_group() != COLGROUP_MOVING
        && moving_object->get_group() != COLGROUP_MOVING_STATIC)
       || !moving_object->is_valid())
      continue;

    for(MovingObjects::iterator i2 = moving_objects.begin();
        i2 != moving_objects.end(); ++i2) {
      MovingObject* moving_object_2 = *i2;
      if(moving_object_2->get_group() != COLGROUP_TOUCHABLE
         || !moving_object_2->is_valid())
        continue;

      if(intersects(moving_object->dest, moving_object_2->dest)) {
        Vector normal;
        CollisionHit hit;
        get_hit_normal(moving_object->dest, moving_object_2->dest,
                       hit, normal);
        if(!moving_object->collides(*moving_object_2, hit))
          continue;
        if(!moving_object_2->collides(*moving_object, hit))
          continue;

        moving_object->collision(*moving_object_2, hit);
        moving_object_2->collision(*moving_object, hit);
      }
    }
  }

  // part3: COLGROUP_MOVING vs COLGROUP_MOVING
  for(MovingObjects::iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    MovingObject* moving_object = *i;

    if((moving_object->get_group() != COLGROUP_MOVING
        && moving_object->get_group() != COLGROUP_MOVING_STATIC)
       || !moving_object->is_valid())
      continue;

    for(MovingObjects::iterator i2 = i+1;
        i2 != moving_objects.end(); ++i2) {
      MovingObject* moving_object_2 = *i2;
      if((moving_object_2->get_group() != COLGROUP_MOVING
          && moving_object_2->get_group() != COLGROUP_MOVING_STATIC)
         || !moving_object_2->is_valid())
        continue;

      collision_object(moving_object, moving_object_2);
    }
  }

  // apply object movement
  for(MovingObjects::iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    MovingObject* moving_object = *i;

    moving_object->bbox = moving_object->dest;
    moving_object->movement = Vector(0, 0);
  }
}

bool
Sector::is_free_of_tiles(const Rectf& rect, const bool ignoreUnisolid) const
{
  using namespace collision;

  for(std::list<TileMap*>::const_iterator i = solid_tilemaps.begin(); i != solid_tilemaps.end(); i++) {
    TileMap* solids = *i;

    // test with all tiles in this rectangle
    Rect test_tiles = solids->get_tiles_overlapping(rect);

    for(int x = test_tiles.left; x < test_tiles.right; ++x) {
      for(int y = test_tiles.top; y < test_tiles.bottom; ++y) {
        const Tile* tile = solids->get_tile(x, y);
        if(!tile) continue;
        if(!(tile->getAttributes() & Tile::SOLID))
          continue;
        if((tile->getAttributes() & Tile::UNISOLID) && ignoreUnisolid)
          continue;
        if(tile->getAttributes() & Tile::SLOPE) {
          AATriangle triangle;
          Rectf tbbox = solids->get_tile_bbox(x, y);
          triangle = AATriangle(tbbox, tile->getData());
          Constraints constraints;
          if(!collision::rectangle_aatriangle(&constraints, rect, triangle))
            continue;
        }
        // We have a solid tile that overlaps the given rectangle.
        return false;
      }
    }
  }

  return true;
}

bool
Sector::is_free_of_statics(const Rectf& rect, const MovingObject* ignore_object, const bool ignoreUnisolid) const
{
  using namespace collision;

  if (!is_free_of_tiles(rect, ignoreUnisolid)) return false;

  for(MovingObjects::const_iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    const MovingObject* moving_object = *i;
    if (moving_object == ignore_object) continue;
    if (!moving_object->is_valid()) continue;
    if (moving_object->get_group() == COLGROUP_STATIC) {
      if(intersects(rect, moving_object->get_bbox())) return false;
    }
  }

  return true;
}

bool
Sector::is_free_of_movingstatics(const Rectf& rect, const MovingObject* ignore_object) const
{
  using namespace collision;

  if (!is_free_of_tiles(rect)) return false;

  for(MovingObjects::const_iterator i = moving_objects.begin();
      i != moving_objects.end(); ++i) {
    const MovingObject* moving_object = *i;
    if (moving_object == ignore_object) continue;
    if (!moving_object->is_valid()) continue;
    if ((moving_object->get_group() == COLGROUP_MOVING)
        || (moving_object->get_group() == COLGROUP_MOVING_STATIC)
        || (moving_object->get_group() == COLGROUP_STATIC)) {
      if(intersects(rect, moving_object->get_bbox())) return false;
    }
  }

  return true;
}

bool
Sector::add_bullet(const Vector& pos, const PlayerStatus* player_status, float xm, Direction dir)
{
  // TODO remove this function and move these checks elsewhere...

  Bullet* new_bullet = 0;
  if((player_status->bonus == FIRE_BONUS &&
      (int)bullets.size() >= player_status->max_fire_bullets) ||
     (player_status->bonus == ICE_BONUS &&
      (int)bullets.size() >= player_status->max_ice_bullets))
    return false;
  new_bullet = new Bullet(pos, xm, dir, player_status->bonus);
  add_object(new_bullet);

  sound_manager->play("sounds/shoot.wav");

  return true;
}

bool
Sector::add_smoke_cloud(const Vector& pos)
{
  add_object(new SmokeCloud(pos));
  return true;
}

void
Sector::play_music(MusicType type)
{
  currentmusic = type;
  switch(currentmusic) {
    case LEVEL_MUSIC:
      sound_manager->play_music(music);
      break;
    case HERRING_MUSIC:
      sound_manager->play_music("music/invincible.music");
      break;
    case HERRING_WARNING_MUSIC:
      sound_manager->stop_music(TUX_INVINCIBLE_TIME_WARNING);
      break;
    default:
      sound_manager->play_music("");
      break;
  }
}

MusicType
Sector::get_music_type()
{
  return currentmusic;
}

int
Sector::get_total_badguys()
{
  int total_badguys = 0;
  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    BadGuy* badguy = dynamic_cast<BadGuy*> (*i);
    if (badguy && badguy->countMe)
      total_badguys++;
  }

  return total_badguys;
}

bool
Sector::inside(const Rectf& rect) const
{
  for(std::list<TileMap*>::const_iterator i = solid_tilemaps.begin(); i != solid_tilemaps.end(); i++) {
    TileMap* solids = *i;

    Rectf bbox = solids->get_bbox();
    bbox.p1.y = -INFINITY; // pretend the tilemap extends infinitely far upwards

    if (bbox.contains(rect))
      return true;
  }
  return false;
}

float
Sector::get_width() const
{
  float width = 0;
  for(std::list<TileMap*>::const_iterator i = solid_tilemaps.begin();
      i != solid_tilemaps.end(); i++) {
    TileMap* solids = *i;
    width = std::max(width, solids->get_bbox().get_right());
  }

  return width;
}

float
Sector::get_height() const
{
  float height = 0;
  for(std::list<TileMap*>::const_iterator i = solid_tilemaps.begin();
      i != solid_tilemaps.end(); i++) {
    TileMap* solids = *i;
    height = std::max(height, solids->get_bbox().get_bottom());
  }

  return height;
}

void
Sector::change_solid_tiles(uint32_t old_tile_id, uint32_t new_tile_id)
{
  for(std::list<TileMap*>::const_iterator i = solid_tilemaps.begin(); i != solid_tilemaps.end(); i++) {
    TileMap* solids = *i;
    solids->change_all(old_tile_id, new_tile_id);
  }
}

void
Sector::set_ambient_light(float red, float green, float blue)
{
  ambient_light.red = red;
  ambient_light.green = green;
  ambient_light.blue = blue;
}

float
Sector::get_ambient_red()
{
  return ambient_light.red;
}

float
Sector::get_ambient_green()
{
  return ambient_light.green;
}

float
Sector::get_ambient_blue()
{
  return ambient_light.blue;
}

void
Sector::set_gravity(float gravity)
{
  log_warning << "Changing a Sector's gravitational constant might have unforeseen side-effects" << std::endl;
  this->gravity = gravity;
}

float
Sector::get_gravity() const
{
  return gravity;
}

/* EOF */
