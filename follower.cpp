#include "follower.h"
#include "behaviour.h"

#include "ship.h"
#include <iostream>
#include <cstdlib>
#include <cmath>
using namespace std;

Follower::Follower(Ship *ship) : Behaviour(ship), asteroids(NULL) {
  common_init();
}

Follower::Follower(Ship *ship, list<Object *> *targets) : Behaviour(ship), targets(targets), asteroids(NULL) {
  common_init();
}

Follower::Follower(Ship *ship, list<Object *> *targets, list<Object *> *asteroids) : Behaviour(ship), targets(targets), asteroids(asteroids) {
  common_init();
}

Follower::~Follower() {
  ship->rotate_right(false);
  ship->thrust(false);
  delete targets;
}

void Follower::common_init() {
  time_until_next_lock = 2500.0 + rand()%500;
  time_between_locks = 900 + rand()%1000;
  shoot_timer = 0;
  target = NULL;
  done = false;
}

bool Follower::compute_avoidance(float &avoidance_angle) {
  static const float AVOID_RANGE = 350.0f;

  if(!asteroids) return false;

  Object *closest = NULL;
  float closest_dist = AVOID_RANGE;

  list<Object *>::iterator it;
  for(it = asteroids->begin(); it != asteroids->end(); ++it) {
    Object *a = *it;
    if(!a->alive) continue;
    float dist = ship->position.distance_to(a->position) - a->radius;
    if(dist < closest_dist) {
      closest_dist = dist;
      closest = a;
    }
  }

  if(!closest) return false;

  // Vector from asteroid toward ship (direction to steer)
  WrappedPoint roid_pos = closest->position;
  Point away = ship->position.closest_to(roid_pos) - roid_pos;
  avoidance_angle = ship->heading() - away.normalized().direction();
  avoidance_angle = fmod(avoidance_angle, 360.0f);
  if(avoidance_angle < 0.0f) avoidance_angle += 360.0f;
  return true;
}

void Follower::step(int delta) {
  if(ship->is_alive()) {
    lock_step(delta);

    if(target) {
      if (target->is_alive()) {
        ship->thrust(true);

        float avoidance_angle;
        if(compute_avoidance(avoidance_angle)) {
          // Steer away from asteroid, ignore target rotation this frame
          if(avoidance_angle >= 0 && avoidance_angle < 180) {
            ship->rotate_right(true);
            ship->rotate_left(false);
          } else {
            ship->rotate_left(true);
            ship->rotate_right(false);
          }
        } else {
          WrappedPoint target_point = target->position;
          float angle = (ship->heading() - (ship->position.closest_to(target_point) - target_point).normalized().direction());
          angle = (angle < 0.0) ? (360.0 + angle) : angle;
          if (angle >= 0 && angle < 180) {
            ship->rotate_left(true);
          } else {
            ship->rotate_right(true);
          }
          burst_shooting_step(delta, angle, target_point);
        }
      } else {
        target = NULL;
        time_until_next_lock = time_between_locks;
        ship->rotate_right(false);
      }
    }
  }
}

void Follower::burst_shooting_step(int delta, float angle, const WrappedPoint &target_point) {
  static const float SHOOT_RANGE   = 600.0f;
  static const float FACING_CONE   = 25.0f;
  static const int   SHOOT_INTERVAL = 3000;

  shoot_timer -= delta;
  if(shoot_timer > 0) return;

  bool in_range  = ship->position.distance_to(target_point) < SHOOT_RANGE;
  bool facing    = angle > 180.0f - FACING_CONE && angle < 180.0f + FACING_CONE;

  if(in_range && facing) {
    ship->shoot(true);
    shoot_timer = SHOOT_INTERVAL;
  }
}

void Follower::lock_nearest_target() {
  if(target && !target->is_alive()) {
    target = NULL;
  }

  list<Object *>::iterator s;
  for(s = targets->begin(); s != targets->end(); s++) {
    if((*s)->is_alive()) {
      if(target == NULL || (*s)->position.distance_to(ship->position) < target->position.distance_to(ship->position)) {
        target = (*s);
      }
    }
  }
}

void Follower::lock_step(int delta) {
  time_until_next_lock -= delta;
  if(time_until_next_lock <= 0) {
    lock_nearest_target();
    time_until_next_lock += time_between_locks;
  }
}
