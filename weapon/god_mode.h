#ifndef GOD_MODE_H
#define GOD_MODE_H

#include "base.h"

namespace Weapon {
  class GodMode : public Base {
  public:
    GodMode(Ship *ship, int duration_ms = 10000);
    void shoot(bool on = true) { shooting = on; }
    void step(int delta);
    int time_remaining() const { return _ammo; }
  private:
    int time_between_shots = 150;
    int time_until_next_shot = 0;
  };
}

#endif
