#include "enemy.h"

#include <vector>
#include <iostream>
#include "point.h"

Enemy::Enemy(float x, float y, std::vector<Ship*>* targets) : Car(x,y), targets(targets) {
  thrust_force = 0.145;
  rotation_force = 0.375;
  time_until_next_shot = 10000.0;
  time_between_shots = 3000.0;
  thrust(true);
  
  time_until_next_lock = 0.0;
  time_between_locks = 1000.0;
}

void Enemy::lock_nearest_target() {
  target = *targets->begin();
  for(unsigned int i = 1; i < targets->size(); i++) {
    if((*targets)[i]->is_alive() && (*targets)[i]->position.distance_to(position) < target->position.distance_to(position)) {
      target = (*targets)[i];
    }
  }
}

bool Enemy::is_removable() {
  return !alive && bullets.empty();
}

void Enemy::step(float delta) {
  Car::step(delta);
  if(is_alive()) {
    velocity = velocity - velocity * 0.0005 * delta;

    // bool close = (target->position - position).magnitude() < 100.0;
    time_until_next_shot -= delta;
    while(time_until_next_shot <= 0) {
      shoot();
      time_until_next_shot += time_between_shots;
    }
    
    time_until_next_lock -= delta;
    while(time_until_next_lock <= 0) {
      lock_nearest_target();
      time_until_next_lock += time_between_locks;
    }
    
    float angle = (heading() - (position.closest_to(target->position) - target->position).normalized().direction());
    if (angle >= 0 && angle < 180 || angle >= -360 && angle < -180) {
      rotate_left(true);
    } else {
      rotate_right(true);
    }
  }
}
