#include "nova.h"
#include "../ship.h"

namespace Weapon {
  Nova::Nova(Ship *ship) : Base(ship) {
    _name = "NOVA";
    _ammo = 0;
    unlimited = false;
  }

  void Nova::shoot(bool on) {
    shooting = on;
    if (on && _ammo > 0) {
      _ammo--;
      ship->nova_detonate();
      shooting = false;  // single-fire, don't hold
    }
  }
}
