#ifndef ENEMY_H
#define ENEMY_H

#include "car.h"

class Enemy : public Car {
  public:
    Enemy() {};
    Enemy(float x, float y, Ship* target);
    
    void step(float delta);
    bool is_removable();
    
  private:
    Ship* target;
    
    float time_until_next_shot, time_between_shots;
};

#endif
