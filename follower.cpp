#include "follower.h"
#include "behaviour.h"

#include "ship.h"
#include <iostream>
#include <cstdlib>
using namespace std;

Follower::Follower(Ship *ship) : Behaviour(ship) {
  common_init();
}

Follower::Follower(Ship *ship, list<Object *> *targets) : Behaviour(ship), targets(targets) {
  common_init();
}

Follower::~Follower() {
  ship->rotate_right(false);
  ship->thrust(false);
  delete targets;
}

void Follower::common_init() {
  time_until_next_lock = 0.0;
  time_between_locks = 900 + rand()%100;
  target = NULL;
  done = false;
}

void Follower::step(int delta) {
  if(ship->is_alive()) {
    lock_step(delta);

    if(target) {
      if (target->is_alive()) {
        ship->thrust(true);
        WrappedPoint target_point = target->position;
        float angle = (ship->heading() - (ship->position.closest_to(target_point) - target_point).normalized().direction());
        angle = (angle < 0.0) ? (360.0 + angle) : angle;
        if (angle >= 0 && angle < 180) {
          ship->rotate_left(true);
        } else {
          ship->rotate_right(true);
        }
      } else {
       target = NULL;
        time_until_next_lock = 2000.0;
        ship->rotate_right(false);
      }
    }
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
