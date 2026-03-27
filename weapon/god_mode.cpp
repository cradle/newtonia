#include "god_mode.h"
#include "../ship.h"

namespace Weapon {
  GodMode::GodMode(Ship *ship, int duration_ms) : Base(ship) {
    _name = "GOD MODE";
    _ammo = duration_ms;
    unlimited = false;
    ship->invincible = true;
    // Set very high so Ship::step doesn't expire it; GodMode::step manages the timer
    ship->time_left_invincible = INT_MAX;
  }

  void GodMode::step(int delta) {
    if (_ammo <= 0) return;
    _ammo -= delta;
    if (_ammo <= 0) {
      _ammo = 0;
      ship->invincible = false;
      ship->time_left_invincible = 0;
    }
  }
}
