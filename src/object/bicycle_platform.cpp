//  SuperTux - BicyclePlatform
//  Copyright (C) 2007 Christoph Sommer <christoph.sommer@2007.expires.deltadevelopment.de>
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

#include "object/bicycle_platform.hpp"

#include <algorithm>
#include <math.h>

#include "math/util.hpp"
#include "object/player.hpp"
#include "object/portable.hpp"
#include "supertux/debug.hpp"
#include "supertux/sector.hpp"
#include "util/reader_mapping.hpp"

BicyclePlatformChild::BicyclePlatformChild(const ReaderMapping& reader, float angle_offset, BicyclePlatform& parent) :
  MovingSprite(reader, "images/objects/platforms/small.sprite", LAYER_OBJECTS, COLGROUP_STATIC),
  m_parent(parent),
  m_angle_offset(angle_offset),
  m_momentum(),
  m_contacts()
{
}

void
BicyclePlatformChild::update(float dt_sec)
{
  float angle = m_parent.m_angle + m_angle_offset;
  angle = math::positive_fmodf(angle, math::TAU);

  Vector dest = m_parent.m_center + Vector(cosf(angle), sinf(angle)) * m_parent.m_radius - (m_bbox.get_size().as_vector() * 0.5);
  m_movement = dest - get_pos();
}

HitResponse
BicyclePlatformChild::collision(GameObject& other, const CollisionHit& )
{
  const float gravity = Sector::get().get_gravity();

  // somehow the hit parameter does not get filled in, so to determine (hit.top == true) we do this:
  auto mo = dynamic_cast<MovingObject*>(&other);
  if (!mo) return FORCE_MOVE;
  if ((mo->get_bbox().p2.y) > (m_bbox.p1.y + 2)) return FORCE_MOVE;

  auto pl = dynamic_cast<Player*>(mo);
  if (pl) {
    if (pl->is_big()) m_momentum += m_parent.m_momentum_change_rate * gravity;
    auto po = pl->get_grabbed_object();
    auto pomo = dynamic_cast<MovingObject*>(po);
    if (m_contacts.insert(pomo).second) {
      m_momentum += m_parent.m_momentum_change_rate * gravity;
    }
  }

  if (m_contacts.insert(&other).second) {
    m_momentum += m_parent.m_momentum_change_rate * Sector::get().get_gravity();
  }

  return FORCE_MOVE;
}

BicyclePlatform::BicyclePlatform(const ReaderMapping& reader) :
  GameObject(reader),
  m_center(),
  m_radius(128),
  m_angle(0),
  m_angular_speed(0.0f),
  m_momentum_change_rate(0.1f),
  m_children()
{
  reader.get("x", m_center.x);
  reader.get("y", m_center.y);
  reader.get("radius", m_radius, 128.0f);
  reader.get("momentum-change-rate", m_momentum_change_rate, 0.1f);

  int n = 2;
  for(int i = 0; i < n; ++i) {
    const float offset = static_cast<float>(i) * (math::TAU / static_cast<float>(n));
    m_children.push_back(d_sector->add<BicyclePlatformChild>(reader, offset, *this));
  }
}

BicyclePlatform::~BicyclePlatform()
{
}

void
BicyclePlatform::draw(DrawingContext& context)
{
  if (g_debug.show_collision_rects) {
    context.color().draw_filled_rect(Rectf::from_center(m_center, Sizef(16, 16)), Color::MAGENTA, LAYER_OBJECTS);
  }
}

void
BicyclePlatform::update(float dt_sec)
{
  float total_angular_momentum = 0.0f;
  for(auto& child : m_children)
  {
    const float child_angle = m_angle + child->m_angle_offset;
    const float angular_momentum = cosf(child_angle) * child->m_momentum;
    total_angular_momentum += angular_momentum;
    child->m_momentum = 0.0f;
    child->m_contacts.clear();
  }

  m_angular_speed += (total_angular_momentum * dt_sec) * math::PI;
  m_angular_speed *= 1.0f - dt_sec * 0.2f;
  m_angle += m_angular_speed * dt_sec;
  m_angle = math::positive_fmodf(m_angle, math::TAU);

  m_angular_speed = std::min(std::max(m_angular_speed, -128.0f * math::PI * dt_sec),
                             128.0f * math::PI * dt_sec);

  // FIXME: allow travel along a path
  m_center += Vector(m_angular_speed, 0) * dt_sec * 32;
}

void
BicyclePlatform::editor_delete()
{
  for(auto& child : m_children)
  {
    child->remove_me();
  }
}

void
BicyclePlatform::after_editor_set()
{
  GameObject::after_editor_set();
}

ObjectSettings
BicyclePlatform::get_settings()
{
  auto result = GameObject::get_settings();
  result.options.push_back(ObjectOption(MN_NUMFIELD, _("Radius"), &m_radius, "radius"));
  result.options.push_back(ObjectOption(MN_NUMFIELD, _("Momentum change rate"), &m_momentum_change_rate, "momentum-change-rate"));
  return result;
}

/* EOF */
