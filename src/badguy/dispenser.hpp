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

#ifndef HEADER_SUPERTUX_BADGUY_DISPENSER_HPP
#define HEADER_SUPERTUX_BADGUY_DISPENSER_HPP

#include "badguy/badguy.hpp"
#include "scripting/dispenser.hpp"
#include "squirrel/exposed_object.hpp"

class Dispenser final : public BadGuy,
                        public ExposedObject<Dispenser, scripting::Dispenser>
{
public:
  Dispenser(const ReaderMapping& reader);

  virtual void draw(DrawingContext& context) override;
  virtual void activate() override;
  virtual void deactivate() override;
  virtual void active_update(float dt_sec) override;

  virtual void freeze() override;
  virtual void unfreeze() override;
  virtual bool is_freezable() const override;
  virtual bool is_flammable() const override;
  virtual std::string get_class() const override {
    return "dispenser";
  }
  virtual std::string get_display_name() const override {
    return _("Dispenser");
  }
  virtual std::string get_type_string() const {
    switch(type) {
    case DT_DROPPER:
      return "dropper";
    case DT_ROCKETLAUNCHER:
      return "rocketlauncher";
    case DT_CANNON:
      return "cannon";
    case DT_POINT:
      return "point";
    default:
      return "unknown";
    }
  }

  virtual ObjectSettings get_settings() override;
  virtual void after_editor_set() override;

  void notify_dead()
  {
    if(limit_dispensed_badguys)
    {
      current_badguys--;
    }
  }

protected:
  virtual bool collision_squished(GameObject& object) override;
  virtual HitResponse collision(GameObject& other, const CollisionHit& hit) override;
  void launch_badguy();

private:

  void set_correct_action();

  float cycle;
  std::vector<std::string> badguys;
  unsigned int next_badguy;
  Timer dispense_timer;
  bool autotarget;
  bool swivel;
  bool broken;
  bool random;

  typedef enum {
    DT_DROPPER, DT_ROCKETLAUNCHER, DT_CANNON, DT_POINT
  } DispenserType;

  DispenserType type;
  std::string type_str;

  DispenserType dispenser_type_from_string(const std::string& type_string) const
  {
    if (type_string == "dropper")
      return DT_DROPPER;
    if (type_string == "rocketlauncher")
      return DT_ROCKETLAUNCHER;
    if (type_string == "cannon")
      return DT_CANNON;
    if (type_string == "point")
      return DT_POINT;
    throw std::exception();
  }

  /**
   * Do we need to limit the number of dispensed badguys?
   */
  bool limit_dispensed_badguys;

  /**
   * Maximum concurrent number of badguys to be dispensed
   */
  int max_concurrent_badguys;

  /**
   * Current amount of spawned badguys
   */
  int current_badguys;
};

#endif

/* EOF */
