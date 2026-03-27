#ifndef GOD_MODE_H
#define GOD_MODE_H

#include "base.h"

namespace Weapon {
  class GodMode : public Base {
  public:
    GodMode(Ship *ship, int duration_ms = 10000);
    void shoot(bool on = true) {}  // auto-activates on creation, no manual trigger
    void step(int delta);
    int time_remaining() const { return _ammo; }
  };
}

#endif
