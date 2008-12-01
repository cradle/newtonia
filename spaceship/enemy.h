#ifndef ENEMY_H
#define ENEMY_H

#include <vector>
#include "car.h"

class Enemy : public Car {
  public:
    Enemy() {};
    Enemy(float x, float y, std::vector<Ship*> * targets, int difficulty = 0);
    
    void step(float delta);
    bool is_removable();
    
  private:
    void lock_nearest_target();
    Ship* target;
    std::vector<Ship*> * targets;
    
    float time_until_next_lock, time_between_locks;
};

#endif
