#ifndef ENEMY_H
#define ENEMY_H

#include <list>
#include "ship.h"

class Enemy : public Ship {
  public:
    Enemy() {};
    Enemy(float x, float y, std::list<Ship*> * targets, int difficulty = 0);
    ~Enemy();
    
    void step(float delta);
    void reset();
    
  private:
    void lock_step(float delta);
    void burst_shooting_step(float delta);
    void lock_nearest_target();
    Ship* target;
    std::list<Ship*> * targets;

    int time_until_next_lock, time_between_locks;
    int burst_time, burst_time_left, time_between_bursts, time_until_next_burst;
};

#endif
