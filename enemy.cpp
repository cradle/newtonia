#include "enemy.h"

#include <list>
#include <iostream>
#include "point.h"

Enemy::Enemy(float x, float y, std::list<Ship*>* targets, int difficulty) : Car(x,y), targets(targets) {
  thrust_force = 0.135 + difficulty*0.00025 + rand()%50/10000.0;
  rotation_force = 0.15 + difficulty*0.01 + rand()%10/1000.0;
  time_until_next_shot = time_between_shots = 333;
  burst_time = burst_time_left = difficulty*25 + 50;
  time_between_bursts = time_until_next_burst = rand()%100 + (5000/(difficulty+1));
  accuracy = 1.0/(difficulty+10.0);
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

  list<Ship*>::iterator s;
  for(s = targets->begin(); s != targets->end(); s++) {
    if((*s)->is_alive()) {
      if(target == NULL || (*s)->position.distance_to(position) < target->position.distance_to(position)) {
        target = (*s);
      }
    }
  }
}

bool Enemy::is_removable() const {
  return !alive && bullets.empty() && debris.empty();
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
        if(!shooting) {
          time_until_next_burst -= delta;

          if(time_until_next_burst <= 0.0) {
            shoot(true);
            burst_time_left = burst_time;
          }
        } else if (burst_time_left >= 0.0) {
          burst_time_left -= delta;
        } else {
          shoot(false);
          time_until_next_burst = time_between_bursts;
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
    		shoot(false);
        rotate_right(false);
      }
    }
  }
}
