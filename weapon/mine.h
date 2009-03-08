#ifndef MINE_H
#define MINE_H

#include "base.h"

namespace Weapon {
  class Mine : public Base {
  public:
    Mine(Ship *ship);
    ~Mine();
  
    void shoot(bool on = true);
    void step(int delta);
  };
}

#endif