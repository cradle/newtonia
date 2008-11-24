#include "enemy.h"

#include <iostream>

Enemy::Enemy(float x, float y, Ship* target) : Car(x,y) {
  this->target = target;
  thrust_force = 0.15;
  rotation_force = 0.3;
  thrust(true);
}

void Enemy::step(float delta) {
  Car::step(delta);
  if(is_alive()) {
    velocity = velocity - velocity * 0.0005 * delta;

    float angle = (heading() - (position - target->position).normalized().direction());
    if (angle >= 0 && angle < 90 || angle >= -360 && angle < -270) {
      rotate_left(true);
      // thrust(false);
    } else if (angle >= 90 && angle < 180 || angle > -270 && angle <= -180) {
      rotate_left(true);
      // thrust(true);
    } else if (angle >= 180 && angle <= 270){    
      rotate_right(true);
      // thrust(true);
    } else {
      rotate_right(true);
      // thrust(false);
    }
  }
}