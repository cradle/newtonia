#include "enemy.h"

#include <vector>
#include <iostream>
#include "point.h"

Enemy::Enemy(float x, float y, std::vector<Ship*>* targets, int difficulty) : Car(x,y), targets(targets) {
  thrust_force = 0.135 + difficulty*0.00025 + rand()%50/10000.0;
  rotation_force = 0.15 + difficulty*0.01 + rand()%10/1000.0;
  time_between_shots = 1000.0;
  time_until_next_shot = rand()%(int)time_between_shots;
  accuracy = 0.0;
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

bool Enemy::is_removable() const {
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
