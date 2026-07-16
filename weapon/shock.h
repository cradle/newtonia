#ifndef SHOCK_H
#define SHOCK_H

#include "base.h"
#include "../object.h"
#include "../point.h"
#include "../wrapped_point.h"
#include <vector>
#include <list>
#include <SDL_mixer.h>

class Grid;

// A single chain-lightning bolt. It grows out ahead of the ship one staggered
// segment per tick, and each new segment looks for the nearest asteroid / enemy
// / station / hazard near its advancing tip and bends the *next* segment toward
// it — so the bolt visibly paths to whatever is nearby. When a tip reaches a
// target the target is recorded in `struck`; the owning Ship drains and damages
// asteroid hits, GLGame drains and damages enemy/station/hazard hits (each is
// handled where it has the right API). After a *killing* hit the bolt keeps
// chaining to the next-nearest target (targets already reached are skipped); but
// if a hit fails to destroy the target — a shielded/invincible player, or
// something that survives more than one shot (tough asteroid, station, comet,
// pulsar) — the owner calls stop() so the arc ends there instead of arcing
// onward. Invincible ASTEROIDS never enter `struck` at all: they are not sought
// (seek_target skips them) and grow_segment stops the bolt at their surface
// directly when a segment runs into one (grid-queried, like the bullet sweep).
// The whole thing is transient: it grows for a handful of frames then fades out.
struct ShockBolt {
  std::vector<Point>   points;      // polyline; points[0] == origin at the gun
  Point                main_dir;    // firing direction; the bolt stays in its front 180°
  Point                heading;     // current (un-jittered) growth direction
  float                seg_accum;   // ms accumulated toward the next segment
  bool                 growing;
  float                life;        // fade fraction once grown (1 -> 0)
  bool                 net_reported = false;  // polyline sent to the peer once grown
  bool                 net_display = false;   // received replica: no local kills
  std::vector<Object*> struck;      // targets reached this run, for owners to damage
  std::vector<Object*> avoid;       // targets already chained to (skipped when seeking)
  Object              *owner;       // the ship that fired this bolt; never a seek/hit target

  // Spark burst drawn where an arc was absorbed by something it couldn't
  // destroy. `spark_rays` are endpoint offsets from `spark_pos`, generated once
  // when the arc stops so they don't flicker; `spark_life` fades 1 -> 0.
  Point                spark_pos;
  std::vector<Point>   spark_rays;
  float                spark_life = 0.0f;

  static const float SEGMENT_LEN;   // world units per segment
  static const float SEG_MS;        // ms between segments (~ one per tick)
  static const int   MAX_SEGMENTS;  // bolt length cap
  static const float SEEK_RANGE;    // how far ahead a tip looks for a target
  static const float HIT_RADIUS;    // slack added to a target's radius for a hit
  static const float SPREAD;        // random per-segment heading jitter (radians)
  static const float STAGGER;       // perpendicular endpoint offset (units)
  static const float FADE_MS;       // fade-out duration once fully grown
  static const float SPARK_MS;      // spark-burst fade duration

  ShockBolt(WrappedPoint origin, Point facing_dir, Object *owner = nullptr);
  // `grid` is the asteroid collision grid, used to find invincible rocks the
  // growing segment must stop at (segment query, like the bullet sweep).
  void step_bolt(int delta, std::list<Object*> *asteroids, std::list<Object*> *hostiles,
                 const Grid *grid);
  bool is_alive() const { return life > 0.0f || spark_life > 0.0f; }
  // Halt further growth: the arc has reached a target it couldn't destroy, so it
  // ends where it is and fades instead of chaining onward, spawning a spark at
  // the collision point. Called by the owning Ship / GLGame once a struck target
  // is known to have survived the hit.
  void stop();

private:
  // Nearest live, un-avoided target in `lst` within SEEK_RANGE of `tip`.
  // `asteroid_list` says every element is statically an Asteroid (the ship's
  // missile-asteroid list) — invincible rocks are skipped there (never sought,
  // only blocking); the hostiles list never contains asteroids, so it takes no
  // per-element type check at all. Invincible SHIPS (shielded/god-mode players
  // under friendly fire) stay eligible — the arc paths to and stops at them.
  Object *seek_target(const Point &tip, std::list<Object*> *lst, float &best_dist,
                      bool asteroid_list) const;
  void grow_segment(std::list<Object*> *asteroids, std::list<Object*> *hostiles,
                    const Grid *grid);
  // Where a terminal segment should end: the point on `target`'s surface facing
  // the incoming bolt (`from`), given the target's centre. Traces the real
  // polygon edge for asteroids; falls back to the collision circle otherwise, so
  // the arc lands on the surface instead of piercing to the centre.
  Point surface_hit(Object *target, const Point &center, const Point &from) const;
};

namespace Weapon {
  class Shock : public Base {
  public:
    Shock(Ship *ship);
    ~Shock();

    void shoot(bool on = true) override;
    void step(int delta) override;
    // Semi-automatic (inherits is_automatic()==false): one bolt per trigger
    // pull, no hold-to-repeat — like Beam/Lance.

    // Cooldown accessors: the net client rebuilds the weapon list with fresh
    // objects 10x/s from snapshots; net_apply_state preserves the live cooldown
    // across the rebuild (mirroring the Default gun) so the re-press rate limit
    // stays consistent and a snapshot landing mid-press can't double-fire.
    int  get_cooldown() const { return cooldown; }
    void set_cooldown(int ms) { cooldown = ms; }

  private:
    void try_fire();  // fire a bolt if off cooldown; handles empty ammo
    int cooldown;   // ms until the next bolt may be fired
    Mix_Chunk *shoot_sound = NULL, *empty_sound = NULL;
  };
}

#endif
