//  SuperTux - MagicBlock
//
//  Magic Blocks are tile-like game objects that are sensitive to
//  lighting conditions. They are rendered in a color and
//  will only be solid as long as light of the same color shines
//  on the block. The black block becomes solid, if any kind of
//  light is above MIN_INTENSITY.
//
//  Copyright (C) 2006 Wolfgang Becker <uafr@gmx.de>
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

#ifndef HEADER_SUPERTUX_OBJECT_MAGICBLOCK_HPP
#define HEADER_SUPERTUX_OBJECT_MAGICBLOCK_HPP

#include "object/moving_sprite.hpp"

class MagicBlock final: public MovingSprite
{
public:
  MagicBlock(const ReaderMapping& reader);

  bool collides(GameObject& other, const CollisionHit& hit) const override;
  virtual HitResponse collision(GameObject& other, const CollisionHit& hit) override;
  virtual void update(float dt_sec) override;
  virtual void draw(DrawingContext& context) override;
  virtual std::string get_class() const override {
    return "magicblock";
  }
  virtual std::string get_display_name() const override {
    return _("Magic block");
  }

  virtual ObjectSettings get_settings() override;
  virtual void after_editor_set() override;

private:
  bool is_solid;
  float trigger_red;
  float trigger_green;
  float trigger_blue;
  float solid_time;
  float switch_delay; /**< seconds until switching solidity */
  Rectf solid_box;
  Color color;
  Color light;
  Vector center;
  bool black;
};

#endif

/* EOF */
