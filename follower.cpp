#include "follower.h"
#include "behaviour.h"

#include "grid.h"
#include "seek.h"
#include "ship.h"
#include <iostream>
#include <cstdlib>
#include <cmath>
using namespace std;

Follower::Follower(Ship *ship) : Behaviour(ship), targets(NULL), asteroids(NULL), difficulty(0.0f), aim_lead(0.0f), shoot_interval_ms(3000), mode(GUNNER), grid(NULL), owns_targets(false) {
  common_init();
}

Follower::Follower(Ship *ship, list<Object *> *targets) : Behaviour(ship), targets(targets), asteroids(NULL), difficulty(0.0f), aim_lead(0.0f), shoot_interval_ms(3000), mode(GUNNER), grid(NULL), owns_targets(false) {
  common_init();
}

Follower::Follower(Ship *ship, list<Object *> *targets, list<Object *> *asteroids, float difficulty, float aim_lead, int shoot_interval_ms, Mode mode, const Grid *grid) : Behaviour(ship), targets(targets), asteroids(asteroids), difficulty(difficulty), aim_lead(aim_lead), shoot_interval_ms(shoot_interval_ms), mode(mode), grid(grid), owns_targets(true) {
  common_init();
}

Follower::~Follower() {
  ship->rotate_right(false);
  ship->thrust(false);
  if (owns_targets) delete targets;
}

void Follower::common_init() {
  time_until_next_lock = 2500.0 + rand()%500;
  time_between_locks = 900 + rand()%1000;
  shoot_timer = 0;
  backing_off = false;
  target = NULL;
  done = false;
}

bool Follower::compute_avoidance(float &avoidance_angle, float &avoidance_strength) {
  // Avoidance range and FOV widen with difficulty so higher-level enemies navigate better.
  float avoid_range = 250.0f + difficulty * 5.0f;
  float fov = 50.0f + difficulty * 4.0f;
  if(fov > 160.0f) fov = 160.0f;

  if(!asteroids) return false;

  // Accumulate a repulsion vector from all nearby asteroids within the FOV cone.
  // Each asteroid contributes a vector pointing away from it, weighted by 1/dist
  // (closer asteroids exert stronger repulsion).
  float sum_x = 0.0f, sum_y = 0.0f;

  // Candidates come from the grid when we have one (convention 6 — this
  // scan runs every 8 ms per enemy, and late-game waves multiplied it);
  // query_radius pads by a whole cell, which covers asteroid radii. The
  // list walk survives only for grid-less legacy users.
  static std::vector<Object *> s_cand;
  if(grid) {
    grid->query_radius(Point(ship->position.x(), ship->position.y()),
                       avoid_range, s_cand);
    // The broad-phase returns an asteroid once per CELL its body overlaps.
    // A duplicate is harmless to the min-test seeks, but this loop
    // ACCUMULATES repulsion — counted twice, a boundary-straddling rock
    // would out-shout its neighbours. Dedup IN QUERY ORDER (a prefix scan;
    // candidate counts are a handful of cells' worth) — sorting by raw
    // pointer would make the float-summation order heap-address-dependent,
    // the sim's first, and churn the shot harness's committed renders.
    size_t n_uniq = 0;
    for (size_t i = 0; i < s_cand.size(); i++) {
      bool dup = false;
      for (size_t j = 0; j < n_uniq && !dup; j++)
        dup = s_cand[j] == s_cand[i];
      if (!dup) s_cand[n_uniq++] = s_cand[i];
    }
    s_cand.resize(n_uniq);
  } else {
    s_cand.assign(asteroids->begin(), asteroids->end());
  }
  for(Object *a : s_cand) {
    if(!a->alive) continue;
    float dist = ship->position.distance_to(a->position) - a->radius;
    if(dist < 1.0f) dist = 1.0f;
    if(dist >= avoid_range) continue;

    WrappedPoint apos = a->position;
    Point toward = (ship->position.closest_to(apos) - apos) * -1.0f;  // ship → asteroid
    float angle_to = ship->heading() - toward.normalized().direction();
    angle_to = fmod(angle_to, 360.0f);
    if(angle_to > 180.0f)  angle_to -= 360.0f;
    if(angle_to < -180.0f) angle_to += 360.0f;
    if(fabs(angle_to) > fov) continue;

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
      float dist = ship->position.distance_to(target->position);

      // Attack runs (see the header note): the ship bores in ON TARGET
      // from outside gun range, fires through the closing window, breaks
      // off before the pass becomes a ram, and extends back out for the
      // next run. The break stretches with CLOSING SPEED (a fast
      // interceptor turns away much further out than the plodding
      // standard ship) but is CAPPED below the 600 gun range — an
      // uncapped stretch broke off at the edge of gun range and the ship
      // never spent a moment on target (field, 2026-08-23). The resume
      // sits beyond gun range and beyond any possible break, so the
      // hysteresis can never re-break instantly and every re-engage is a
      // full firing pass.
      static const float BREAK_RANGE        = 180.0f;
      static const float BREAK_MAX          = 350.0f;
      static const float RESUME_RANGE       = 750.0f;
      static const float BREAK_LOOKAHEAD_MS = 800.0f;
      Point to_target = (ship->position.closest_to(target->position) -
                         target->position).normalized() * -1.0f;
      Point rel_vel = ship->velocity - target->velocity;
      float closing = rel_vel.x() * to_target.x() + rel_vel.y() * to_target.y();
      float brk = BREAK_RANGE +
                  (closing > 0.0f ? closing * BREAK_LOOKAHEAD_MS : 0.0f);
      if(brk > BREAK_MAX) brk = BREAK_MAX;
      // The RAMMER is the one shape that never breaks off — connecting IS
      // its attack (see the header note). Everything else flies runs.
      if(mode != RAMMER) {
        if(!backing_off && dist < brk)                backing_off = true;
        else if(backing_off && dist >= RESUME_RANGE)  backing_off = false;
      }

      if(backing_off) {
        // Same angle convention as the pursuit below (180 = nose on the
        // target, 0 = tail-on), driven the opposite way: turn until the
        // target is astern, thrusting the whole way through — the break
        // starts early enough (the lookahead above) that the continuous-
        // thrust arc clears a stationary target, so the flyby reads as a
        // strafing curve, never an engine cut. No shot gate — the gun
        // only fires facing the target, which we no longer are.
        float angle = (ship->heading() - (ship->position.closest_to(target->position) - target->position).normalized().direction());
        angle = (angle < 0.0) ? (360.0 + angle) : angle;
        if(angle >= 0 && angle < 180) {
          ship->rotate_right(true);
        } else {
          ship->rotate_left(true);
        }
        ship->thrust_analog = 1.0f;
        ship->thrust(true);
        return;
      }

      // BOMBER: artillery stands off. Thrust cuts inside STANDOFF_RANGE so
      // it drifts and turns to aim rather than closing to the keep-distance
      // band at all.
      static const float STANDOFF_RANGE = 700.0f;
      bool advance = mode != BOMBER || dist > STANDOFF_RANGE;
      ship->thrust_analog = 1.0f;
      ship->thrust(advance);
      WrappedPoint target_point = target->position;
      if(aim_lead > 0.0f) {
        // Lead the target: steer toward (and gate the shot on) the point the
        // shared intercept solve picks, blended by aim_lead — the generation
        // ramp GLGame hands down through the station. The gun mints bullets
        // at 0.615 units/ms with 0.99 of the ship's drift and a 2000 ms TTL
        // (weapon/default.cpp fire_shot) — the same kinematics the turret
        // leads with. The ship both flies and shoots along its facing, so
        // steering at the intercept also turns tail-chasing into cut-offs.
        Point self = ship->position.closest_to(target_point);
        Point rel(target_point.x() - self.x(), target_point.y() - self.y());
        Point off;
        if(mode == RAMMER) {
          // The rammer's "projectile" is the hull itself: lead with a
          // nominal ram closing speed over the hunter's long horizon
          // (hazard.cpp's exact pattern), not the gun's bullet kinematics
          // — the 0.615 solve under-led a crossing target by seconds and
          // the charge curved in behind instead of cutting the corner.
          static const float RAM_SPEED = 0.3f;
          off = intercept_offset(rel, target->velocity, RAM_SPEED, 6000.0f,
                                 aim_lead);
        } else {
          Point rel_vel(target->velocity.x() - ship->velocity.x() * 0.99f,
                        target->velocity.y() - ship->velocity.y() * 0.99f);
          off = intercept_offset(rel, rel_vel, 0.615f, 2000.0f, aim_lead);
        }
        target_point = WrappedPoint(target_point.x() + off.x(),
                                    target_point.y() + off.y());
      }
      float angle = (ship->heading() - (ship->position.closest_to(target_point) - target_point).normalized().direction());
      angle = (angle < 0.0) ? (360.0 + angle) : angle;
      if(angle >= 0 && angle < 180) {
        ship->rotate_left(true);
      } else {
        ship->rotate_right(true);
      }
      if(mode != RAMMER)  // the rammer carries no gun — contact is the shot
        burst_shooting_step(delta, angle, target_point);
    }
  }
}

void Follower::burst_shooting_step(int delta, float angle, const WrappedPoint &target_point) {
  static const float SHOOT_RANGE   = 600.0f;
  static const float BOMBER_RANGE  = 900.0f;   // artillery reach: shell flight is 600, lead closes the rest
  static const float FACING_CONE   = 25.0f;

  shoot_timer -= delta;
  if(shoot_timer > 0) return;

  float range = mode == BOMBER ? BOMBER_RANGE : SHOOT_RANGE;
  bool in_range  = ship->position.distance_to(target_point) < range;
  bool facing    = angle > 180.0f - FACING_CONE && angle < 180.0f + FACING_CONE;

  if(in_range && facing) {
    if(mode == BOMBER) ship->fire_bomb();
    else               ship->shoot(true);
    shoot_timer = shoot_interval_ms;
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
