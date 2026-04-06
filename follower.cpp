#include "follower.h"
#include "behaviour.h"

#include "ship.h"
#include <iostream>
#include <cstdlib>
#include <cmath>
using namespace std;

Follower::Follower(Ship *ship) : Behaviour(ship), asteroids(NULL), difficulty(0.0f) {
  common_init();
}

Follower::Follower(Ship *ship, list<Object *> *targets) : Behaviour(ship), targets(targets), asteroids(NULL), difficulty(0.0f) {
  common_init();
}

Follower::Follower(Ship *ship, list<Object *> *targets, list<Object *> *asteroids, float difficulty) : Behaviour(ship), targets(targets), asteroids(asteroids), difficulty(difficulty) {
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

bool Follower::compute_avoidance(float &avoidance_angle, float &avoidance_strength) {
  // Avoidance range widens with difficulty so higher-level enemies detect obstacles sooner.
  float avoid_range = 250.0f + difficulty * 5.0f;

  if(!asteroids) return false;

  // Accumulate a repulsion vector from all nearby asteroids.
  // Each asteroid contributes a vector pointing away from it, weighted by 1/dist
  // (closer asteroids exert stronger repulsion). Full 360° awareness — no FOV cone.
  float sum_x = 0.0f, sum_y = 0.0f;

  list<Object *>::iterator it;
  for(it = asteroids->begin(); it != asteroids->end(); ++it) {
    Object *a = *it;
    if(!a->alive) continue;
    float dist = ship->position.distance_to(a->position) - a->radius;
    if(dist < 1.0f) dist = 1.0f;
    if(dist >= avoid_range) continue;

    WrappedPoint apos = a->position;
    Point away = ship->position.closest_to(apos) - apos;
    float weight = 1.0f / dist;
    sum_x += away.normalized().x() * weight;
    sum_y += away.normalized().y() * weight;
  }

  if(sum_x == 0.0f && sum_y == 0.0f) return false;

  Point composite(sum_x, sum_y);
  avoidance_angle = ship->heading() - composite.normalized().direction();
  avoidance_angle = fmod(avoidance_angle, 360.0f);
  if(avoidance_angle < 0.0f) avoidance_angle += 360.0f;

  // Normalise strength to 0–1 using a smooth asymptote.
  // raw ~0.05 (one asteroid at ~20 units) → strength ~0.5.
  float raw = composite.magnitude();
  avoidance_strength = raw / (raw + 0.05f);

  return true;
}

void Follower::step(int delta) {
  if(ship->is_alive()) {
    lock_step(delta);

    float avoidance_angle, avoidance_strength;
    bool avoiding = compute_avoidance(avoidance_angle, avoidance_strength);

    if(target && !target->is_alive()) {
      target = NULL;
      time_until_next_lock = time_between_locks;
      ship->rotate_right(false);
    }

    if(avoiding) {
      if(avoidance_angle >= 0 && avoidance_angle < 180) {
        ship->rotate_right(true);
        ship->rotate_left(false);
      } else {
        ship->rotate_left(true);
        ship->rotate_right(false);
      }
      float t = 1.0f - avoidance_strength;
      if(t < 0.3f) t = 0.3f;
      ship->thrust_analog = t;
      ship->thrust(true);
    } else if(target) {
      ship->thrust_analog = 1.0f;
      ship->thrust(true);
      WrappedPoint target_point = target->position;
      float angle = (ship->heading() - (ship->position.closest_to(target_point) - target_point).normalized().direction());
      angle = (angle < 0.0) ? (360.0 + angle) : angle;
      if(angle >= 0 && angle < 180) {
        ship->rotate_left(true);
      } else {
        ship->rotate_right(true);
      }
      burst_shooting_step(delta, angle, target_point);
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

void Follower::lock_now() {
  time_until_next_lock = 0;
  lock_nearest_target();
}

void Follower::lock_step(int delta) {
  time_until_next_lock -= delta;
  if(time_until_next_lock <= 0) {
    lock_nearest_target();
    time_until_next_lock += time_between_locks;
  }
}
