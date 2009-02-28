#ifndef DEFAULT_H
#define DEFAULT_H

#include "base.h"

namespace Weapon {
  class Default : public Base {
  public:
    Default(Ship *ship);
    ~Default();
  
    void shoot(bool on = true);
    void step(int delta);
    
  private:
    void fire_shot();
    
    bool shooting, automatic;
    float accuracy;
    Ship *ship;
    int time_until_next_shot, time_between_shots;
  };
}

#endif