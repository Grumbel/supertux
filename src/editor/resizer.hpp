//  SuperTux
//  Copyright (C) 2016 Hume2 <teratux.mail@gmail.com>
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

#ifndef HEADER_SUPERTUX_EDITOR_RESIZER_HPP
#define HEADER_SUPERTUX_EDITOR_RESIZER_HPP

#include "editor/point_marker.hpp"

class Resizer : public PointMarker
{
  public:
    enum Side{
      NONE,
      LEFT_UP,
      RIGHT_DOWN
    };

    Resizer(Rectf* rect_, Side vert_, Side horz_);

    virtual void update(float dt_sec) override;
    virtual void move_to(const Vector& pos) override;
    virtual Vector get_point_vector() const override;
    virtual Vector get_offset() const override;

    virtual bool is_saveable() const override {
      return false;
    }

    void refresh_pos();

  private:
    Rectf* rect;
    Side vert;
    Side horz;

  private:
    Resizer(const Resizer&) = delete;
    Resizer& operator=(const Resizer&) = delete;
};

#endif

/* EOF */
