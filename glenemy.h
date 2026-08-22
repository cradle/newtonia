#ifndef GLENEMY_H
#define GLENEMY_H

#include "glship.h"
#include "object.h"
#include <list>

class GLEnemy : public GLShip {
public:
  // interceptor: the fast fragile flanker (station waves, generation >= 15).
  // The variant is fully encoded in the stats Save::Enemy already carries —
  // a restore/rebuild re-derives it as thrust_force >= INTERCEPTOR_THRUST_MIN
  // (standard hulls top out ~0.135, interceptors start at 0.162), so no
  // savegame or protocol change exists for it. Design rule (CLAUDE.md):
  // its speed stays BELOW the player's plain thrust (0.2) — a straight-line
  // fleeing player always escapes without boost, on every input device;
  // the threat is its tighter fire cadence and corner-cutting intercept
  // steering, never a chase the player cannot win.
  GLEnemy(const Grid &grid, float x, float y, std::list<GLShip*> * target, float difficulty = 1, std::list<Object*> *asteroids = NULL, float aim_lead = 0.0f, bool interceptor = false);
  virtual ~GLEnemy();

  static const float INTERCEPTOR_THRUST_MIN;  // variant-derivation threshold
};

#endif
