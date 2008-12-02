#include "enemy.h"

#include <vector>
#include <iostream>
#include "point.h"

Enemy::Enemy(float x, float y, std::vector<Ship*>* targets, int difficulty) : Car(x,y), targets(targets) {
  thrust_force = 0.145 + difficulty*0.00025;
  rotation_force = 0.375 + difficulty*0.00025;
  time_until_next_shot = 900.0 + rand()%100;
  accuracy = 0.0;
  time_between_shots = 1000.0;
  thrust(true);
  value = 50 + difficulty * 50;
  explode();
  respawns = false;
  
  time_until_next_lock = 0.0;
  time_between_locks = 900.0 + rand()%100;
  
  target = NULL;
}

void Enemy::lock_nearest_target() {
  if(target && !target->is_alive()) {
    target = NULL;
  }

  for(unsigned int i = 0; i < targets->size(); i++) {
    if((*targets)[i]->is_alive()) {
      if(target == NULL || (*targets)[i]->position.distance_to(position) < target->position.distance_to(position)) {
        target = (*targets)[i];
      }
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
    
    time_until_next_lock -= delta;
    if(time_until_next_lock <= 0) {
      lock_nearest_target();
      time_until_next_lock += time_between_locks;
    }
    
    if(target) {
      if(target->is_alive()) {
	  
		// bool close = (target->position - position).magnitude() < 100.0;
		time_until_next_shot -= delta;
		while(time_until_next_shot <= 0) {
		  fire_shot();
		  time_until_next_shot += time_between_shots;
		}
		
        float angle = (heading() - (position.closest_to(target->position) - target->position).normalized().direction());
        if (angle >= 0 && angle < 180 || angle >= -360 && angle < -180) {
          rotate_left(true);
        } else {
          rotate_right(true);
        }
      } else {
		target = NULL;
        time_until_next_lock = 2000.0;
		time_until_next_shot = time_until_next_lock + time_between_shots;
        rotate_right(false);
      }
    }
  }
}
