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

  // Trajectory scan: look further out to catch fast-moving side/rear threats.
  static const float SCAN_RANGE    = 700.0f;  // broader initial filter (center-to-center)
  static const float TIME_HORIZON  = 2500.0f; // ms to look ahead for collision courses
  static const float DANGER_MARGIN = 80.0f;   // predicted miss distance to treat as threat

  if(!asteroids) return false;

  // Accumulate a repulsion vector from all nearby or on-course asteroids.
  // Weight by 1/effective_dist² where effective_dist is the predicted closest approach
  // distance for trajectory threats, or current surface distance otherwise.
  float sum_x = 0.0f, sum_y = 0.0f;

  list<Object *>::iterator it;
  for(it = asteroids->begin(); it != asteroids->end(); ++it) {
    Object *a = *it;
    if(!a->alive) continue;

    WrappedPoint apos = a->position;
    Point away = ship->position.closest_to(apos) - apos;  // asteroid → ship

    float dist = away.magnitude() - a->radius;  // surface distance
    if(dist < 1.0f) dist = 1.0f;
    if(dist >= SCAN_RANGE) continue;

    // Trajectory prediction: find time of closest approach and predicted miss distance.
    // p = asteroid pos relative to ship = -away
    // v = asteroid velocity relative to ship
    // tca = -(p·v)/|v|² = (away·v)/|v|²
    Point v = a->velocity - ship->velocity;
    float rv2 = v.x()*v.x() + v.y()*v.y();
    float effective_dist = dist;

    if(rv2 > 0.001f) {
      float tca = (away.x()*v.x() + away.y()*v.y()) / rv2;
      if(tca > 0.0f && tca < TIME_HORIZON) {
        // Position of asteroid relative to ship at closest approach: -away + v*tca
        float cx = -away.x() + v.x()*tca;
        float cy = -away.y() + v.y()*tca;
        float miss = sqrtf(cx*cx + cy*cy) - a->radius;
        if(miss < 1.0f) miss = 1.0f;
        // Only override if asteroid will come dangerously close.
        // Scale urgency by how imminent the threat is: distant future = weak signal.
        if(miss < DANGER_MARGIN) {
          float urgency = 1.0f - (tca / TIME_HORIZON);  // 1.0 = now, 0.0 = at horizon
          effective_dist = miss + (dist - miss) * (1.0f - urgency * urgency);
        }
      }
    }

    if(effective_dist >= avoid_range) continue;

    Point away_n = away.normalized();
    float weight = 1.0f / (effective_dist * effective_dist);
    sum_x += away_n.x() * weight;
    sum_y += away_n.y() * weight;
  }

  Point composite(sum_x, sum_y);
  float raw = composite.magnitude();

  // Ignore near-zero sums — they indicate balanced threats with no clear escape direction.
  // Threshold equivalent to one asteroid at ~100 units distance.
  if(raw < 0.0001f) return false;

  avoidance_angle = ship->heading() - composite.normalized().direction();
  avoidance_angle = fmod(avoidance_angle, 360.0f);
  if(avoidance_angle < 0.0f) avoidance_angle += 360.0f;

  // Normalise strength to 0–1 using a smooth asymptote.
  // raw ~0.0004 (one asteroid at ~50 units) → strength ~0.5.
  avoidance_strength = raw / (raw + 0.0004f);

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
      // Thrust scales smoothly with alignment: full when facing the safe direction,
      // zero when pointing directly away. Prevents driving into obstacles during a
      // turn without causing the ship to stop dead when only slightly misaligned.
      float signed_angle = avoidance_angle > 180.0f ? avoidance_angle - 360.0f : avoidance_angle;
      float alignment = cosf(signed_angle * M_PI / 180.0f);
      if(alignment >= 0.0f) {
        float t = alignment * (1.0f - avoidance_strength);
        ship->reverse(false);
        ship->thrust_analog = t;
        ship->thrust(t > 0.05f);
      } else {
        // Facing away from safe direction — reverse to back away from the obstacle.
        float t = -alignment * avoidance_strength;
        ship->thrust(false);
        ship->reverse_analog = t;
        ship->reverse(t > 0.05f);
      }
    } else if(target) {
      ship->reverse(false);
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
