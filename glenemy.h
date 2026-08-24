#ifndef GLENEMY_H
#define GLENEMY_H

#include "glship.h"
#include "object.h"
#include <list>

class GLEnemy : public GLShip {
public:
  // The station's wave ship variants. Each is FULLY encoded in the stats
  // Save::Enemy already carries — a restore/rebuild re-derives it from the
  // thrust band via variant_for_thrust() (the bands never overlap:
  // bomber <= 0.12, standard ~0.129-0.135, rammer 0.140-0.148,
  // interceptor >= 0.15), so no savegame or protocol change exists for
  // any of them.
  //
  // INTERCEPTOR (generation >= 15): the fast fragile flanker. Design rule
  // (CLAUDE.md): its speed stays BELOW the player's plain thrust (0.2) — a
  // straight-line fleeing player always escapes without boost, on every
  // input device; the threat is its tighter fire cadence and corner-cutting
  // intercept steering, never a chase the player cannot win.
  //
  // BOMBER (generation >= 17): the slow rear-line artillery piece. Stands
  // off (Follower::BOMBER) and lobs mortar shells that burst into flak
  // rings (Ship::fire_bomb + GLGame's fuse pass); the counterplay is
  // plain-thrust weaving through the ring gaps, per the same rule.
  //
  // RAMMER (generation >= 20): the deliberate rammer the attack-run work
  // reserved. No gun; Follower::RAMMER steers at the intercept point and
  // never breaks off, so it connects on anyone who stops watching it —
  // the ram kills both hulls through the ordinary Ship::collide physics.
  // Its speed sits between the standard ship and the interceptor, well
  // under the player's 0.2 (the hunter hazard's precedent): a committed
  // straight line always escapes, and inside a firing wave the threat is
  // the attention it taxes, never a chase it can win.
  enum Variant { STANDARD, INTERCEPTOR, BOMBER, RAMMER };

  GLEnemy(const Grid &grid, float x, float y, std::list<GLShip*> * target, float difficulty = 1, std::list<Object*> *asteroids = NULL, float aim_lead = 0.0f, Variant variant = STANDARD);
  virtual ~GLEnemy();

  // Re-derive a hull's variant from the one stat that separates the bands.
  static Variant variant_for_thrust(float thrust_force);

  // The variant this hull was BUILT as (mesh, colour, trail). The net
  // client's in-place reconcile compares it against the record's thrust
  // band and rebuilds the replica on mismatch — stats alone are
  // overwritten every apply, but the mesh is chosen once at construction.
  Variant variant() const { return variant_; }

private:
  Variant variant_;

public:
  static const float INTERCEPTOR_THRUST_MIN;  // band floor: >= is an interceptor
  static const float RAMMER_THRUST_MIN;       // band floor: >= (below interceptor's) is a rammer
  static const float BOMBER_THRUST_MAX;       // band ceiling: <= is a bomber
};

#endif
