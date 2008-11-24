#ifndef ENEMY_H
#define ENEMY_H

#include "car.h"

class Enemy : public Car {
  public:
    Enemy() {};
    Enemy(float x, float y, Ship* target);
    
    virtual void step(float delta);
  private:
    Ship* target;
};

#endif
