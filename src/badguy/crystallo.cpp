//  SuperTux - Crystallo
//  Copyright (C) 2008 Wolfgang Becker <uafr@gmx.de>
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

#include "badguy/crystallo.hpp"

#include "util/reader_mapping.hpp"

Crystallo::Crystallo(const ReaderMapping& reader) :
  WalkingBadguy(reader, "images/creatures/crystallo/crystallo.sprite", "left", "right"),
  radius()
{
  walk_speed = 80;
  max_drop_height = 16;

  reader.get("radius", radius, 100.0f);
}

ObjectSettings
Crystallo::get_settings() {
  ObjectSettings result = WalkingBadguy::get_settings();
  result.options.push_back( ObjectOption(MN_NUMFIELD, _("Radius"), &radius,
                                         "radius"));
  return result;
}

void
Crystallo::active_update(float dt_sec)
{
  if(get_pos().x > (m_start_position.x + radius)){
    if(m_dir != LEFT){
      turn_around();
    }
  }
  if( get_pos().x < (m_start_position.x - radius)){
    if(m_dir != RIGHT){
      turn_around();
    }
  }
  BadGuy::active_update(dt_sec);
}

bool
Crystallo::collision_squished(GameObject& object)
{
  set_action(m_dir == LEFT ? "shattered-left" : "shattered-right", /* loops = */ -1, ANCHOR_BOTTOM);
  kill_squished(object);
  return true;
}

bool
Crystallo::is_flammable() const
{
  return false;
}

/* EOF */
