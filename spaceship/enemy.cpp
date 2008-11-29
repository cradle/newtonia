#include "enemy.h"

#include <iostream>
#include "point.h"

Enemy::Enemy(float x, float y, Ship* target) : Car(x,y) {
  this->target = target;
  thrust_force = 0.145;
  rotation_force = 0.375;
  time_until_next_shot = 10000.0;
  time_between_shots = 3000.0;
  thrust(true);
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
    
    float angle = (heading() - (position.closest_to(target->position) - target->position).normalized().direction());
    if (angle >= 0 && angle < 180 || angle >= -360 && angle < -180) {
      rotate_left(true);
    } else {
      rotate_right(true);
    }
  }
}
