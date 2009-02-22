#include "enemy.h"

#include <list>
#include <iostream>
#include "point.h"

Enemy::Enemy(float x, float y, std::list<Ship*>* targets, int difficulty) : Ship(false), targets(targets) {
  position = WrappedPoint(x,y);
  thrust_force = 0.135 + difficulty*0.00025 + rand()%50/10000.0;
  rotation_force = 0.15 + difficulty*0.01 + rand()%10/1000.0;
  automatic_fire = true;
  time_until_next_shot = time_between_shots = 200;
  burst_time = burst_time_left = difficulty*25 + 50;
  time_between_bursts = time_until_next_burst = rand()%100 + (5000/(difficulty+1));
  accuracy = 1.0/(difficulty+10.0);
  value = 50 + difficulty * 50;
  lives = 1;
  heat_rate = 0.0;
  cool_rate = 1.0;
  time_until_respawn = respawn_time = 0;

  time_until_next_lock = 0.0;
  time_between_locks = 900 + rand()%100;

  target = NULL;
}

Enemy::~Enemy() {
  delete targets;
}

bool Enemy::is_removable() const {
  //TODO: Fix this, why can't it just call Ship::is_removable()?
  return !alive && debris.empty();
}

void Enemy::reset() {
  Ship::reset();
  thrust(true);
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

void Enemy::lock_step(float delta) {
  time_until_next_lock -= delta;
  if(time_until_next_lock <= 0) {
    lock_nearest_target();
    time_until_next_lock += time_between_locks;
  }
}

void Enemy::burst_shooting_step(float delta) {
  if(!shooting) {
    time_until_next_burst -= delta;

    if(time_until_next_burst <= 0.0) {
      shoot(true);
      time_until_next_shot = 0;
      burst_time_left = burst_time;
    }
  } else if (burst_time_left >= 0.0) {
    burst_time_left -= delta;
  } else {
    shoot(false);
    time_until_next_burst = time_between_bursts;
  }
}

void Enemy::step(float delta) {
  Ship::step(delta);
  if(is_alive()) {
    velocity = velocity - velocity * 0.0005 * delta;

    lock_step(delta);

    if(target) {
      if (target->is_alive()) {
        burst_shooting_step(delta);

        // float distance = (target->position - position).magnitude();
        // WrappedPoint target_point = target->position + target->velocity.normalized() * 500.0;
        WrappedPoint target_point = target->position;
        float angle = (heading() - (position.closest_to(target_point) - target_point).normalized().direction());
        angle = (angle < 0.0) ? (360.0 + angle) : angle;
        if (angle >= 0 && angle < 180) {
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
