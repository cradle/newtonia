#ifndef SEEK_H
#define SEEK_H

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

#endif
