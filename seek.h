#ifndef SEEK_H
#define SEEK_H

#include <math.h>

#include "object.h"
#include "point.h"
#include "wrapped_point.h"

// The one nearest-target loop shared by the seeks that hunt asteroids and
// hostiles — the missile (weapon/missile.cpp), the shock bolt
// (weapon/shock.cpp) and the turret drone (weapon/turret.cpp). Before this
// existed each carried its own copy of the same scan, and the codebase had
// five hand-rolled variants of "nearest thing to me"; the parts that
// genuinely differ per weapon — cones, avoid lists, invincibility and
// friendly-fire exclusions — stay in each caller's `keep` filter, and the
// parts they all share live here exactly once:
//
// - the alive check;
// - the toroidal shortest displacement (closest_to across the wrap);
// - the SURFACE-distance metric (centre distance minus the target's
//   radius, so a big rock's near edge outranks a pebble's centre — the
//   missile and shock always measured this way; the turret joined them
//   when it adopted the helper);
// - the running minimum, carried through `best_dist` so a caller can scan
//   several containers (asteroids, then hostiles) and keep the single
//   nearest across all of them. Seed it with the weapon's seek range; on
//   return it holds the winner's distance.
//
// `keep(o, to_o, mag)` is the caller's veto: `to_o` is the wrapped
// shortest displacement from `from` to the candidate and `mag` its
// length, delivered so a cone test never recomputes them. The distance
// bound runs FIRST — the cheapest reject — so the filter only sees
// candidates that could actually win.
//
// Container is a template parameter so the same loop serves a
// std::list<Object*> (the game's live lists) and a std::vector<Object*>
// (a Grid::query_radius broad-phase result, which may hold duplicates —
// harmless here, since a duplicate loses the min test to itself).
template <typename Container, typename Keep>
Object *seek_nearest(const Point &from, const Container *lst,
                     float &best_dist, Keep keep) {
  Object *best = NULL;
  if (!lst) return best;
  for (Object *o : *lst) {
    if (!o->alive) continue;
    Point op = o->position.closest_to(from);
    Point to_o = op - from;
    float mag = to_o.magnitude();
    float d = mag - o->radius;
    if (d >= best_dist) continue;
    if (!keep(o, to_o, mag)) continue;
    best_dist = d;
    best = o;
  }
  return best;
}

// The constant-velocity intercept solve shared by everything that LEADS a
// shot: the turret drone (at full lead) and the hostiles' generation-ramped
// aim (the mini-station's snipe and the station wave ships' Follower).
// `rel_pos` is the shooter→target shortest displacement (already resolved
// across the wrap by the caller's closest_to); `rel_vel` is the target's
// velocity minus the bullet's inherited share of the shooter's — every
// mint in the game gives a bullet 0.99 of its shooter's drift, so what
// matters is motion RELATIVE to that. Solves |R + Vr*t| = speed*t for the
// earliest positive intercept time t (the standard quadratic; both sides
// are units when t is ms — c = |R|^2 > 0 always, so in the common a < 0
// case, target slower than a bullet, exactly one root is positive).
//
// Returns the OFFSET to add to the target's position: Vr * t * lead.
// No intercept — target outrunning the bullet, or reachable only after
// the bullet's own lifetime — returns (0,0), i.e. the caller naturally
// falls back to pointing straight at the target. `lead` in [0,1] scales
// the intercept TIME, so 0 aims at the target now, 1 at the perfect
// intercept, and the middle at where the target will be a fraction of
// the flight time from now — the difficulty ramp's one dial.
inline Point intercept_offset(const Point &rel_pos, const Point &rel_vel,
                              float bullet_speed, float bullet_ttl_ms,
                              float lead) {
  float rx = rel_pos.x(), ry = rel_pos.y();
  float vrx = rel_vel.x(), vry = rel_vel.y();
  float qa = vrx * vrx + vry * vry - bullet_speed * bullet_speed;
  float qb = 2.0f * (rx * vrx + ry * vry);
  float qc = rx * rx + ry * ry;
  float t_hit = -1.0f;
  if (fabsf(qa) < 1e-6f) {
    // Relative speed == bullet speed: the quadratic degenerates to linear.
    if (fabsf(qb) > 1e-6f) t_hit = -qc / qb;
  } else {
    float disc = qb * qb - 4.0f * qa * qc;
    if (disc >= 0.0f) {
      float sq = sqrtf(disc);
      float t1 = (-qb - sq) / (2.0f * qa);
      float t2 = (-qb + sq) / (2.0f * qa);
      if (t1 > 0.0f && t2 > 0.0f) t_hit = t1 < t2 ? t1 : t2;
      else t_hit = t1 > 0.0f ? t1 : t2;
    }
  }
  if (t_hit > 0.0f && t_hit <= bullet_ttl_ms)
    return Point(vrx * t_hit * lead, vry * t_hit * lead);
  return Point(0.0f, 0.0f);
}

#endif
