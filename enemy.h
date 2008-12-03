#ifndef ENEMY_H
#define ENEMY_H

#include <list>
#include "car.h"

class Enemy : public Car {
  public:
    Enemy() {};
    Enemy(float x, float y, std::list<Ship*> * targets, int difficulty = 0);
    
    void step(float delta);
    bool is_removable() const;
    
  private:
    void lock_nearest_target();
    Ship* target;
    std::list<Ship*> * targets;
    
    float time_until_next_lock, time_between_locks;
};

#endif
