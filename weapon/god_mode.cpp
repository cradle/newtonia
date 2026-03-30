#include "god_mode.h"
#include "../ship.h"
namespace Weapon {
  GodMode::GodMode(Ship *ship, int duration_ms) : Base(ship) {
    _name = "GOD MODE";
    _ammo = duration_ms;
    unlimited = false;
    ship->invincible = true;
  }

  void GodMode::step(int delta) {
    time_until_next_shot -= delta;
    if(shooting && time_until_next_shot <= 0) {
      ship->fire_bullet_from_gun();
      time_until_next_shot = time_between_shots;
    }
    if (_ammo <= 0) return;
    _ammo -= delta;
    if (_ammo <= 0) {
      _ammo = 0;
      ship->invincible = false;
      ship->time_left_invincible = 0;
      ship->set_shield_hum(false);
    }
  }
}
