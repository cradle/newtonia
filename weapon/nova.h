#ifndef NOVA_H
#define NOVA_H

#include "base.h"

namespace Weapon {
  class Nova : public Base {
  public:
    Nova(Ship *ship);
    void shoot(bool on = true);
    void step(int delta) {}
  };
}

#endif
