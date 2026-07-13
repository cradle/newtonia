#ifndef SHOCK_H
#define SHOCK_H

#include "base.h"
#include "../object.h"
#include "../point.h"
#include "../wrapped_point.h"
#include <vector>
#include <list>
#include <SDL_mixer.h>

// A single chain-lightning bolt. It grows out ahead of the ship one staggered
// segment per tick, and each new segment looks for the nearest asteroid / enemy
// / station near its advancing tip and bends the *next* segment toward it — so
// the bolt visibly paths to whatever is nearby. When a tip reaches a target the
// target is recorded in `struck`; the owning Ship drains and damages asteroid
// hits, GLGame drains and damages enemy/station hits (each is handled where it
// has the right API). After hitting a target the bolt keeps chaining to the
// next-nearest one (targets already reached are skipped). The whole thing is
// transient: it grows for a handful of frames then fades out.
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

  static const float SEGMENT_LEN;   // world units per segment
  static const float SEG_MS;        // ms between segments (~ one per tick)
  static const int   MAX_SEGMENTS;  // bolt length cap
  static const float SEEK_RANGE;    // how far ahead a tip looks for a target
  static const float HIT_RADIUS;    // slack added to a target's radius for a hit
  static const float SPREAD;        // random per-segment heading jitter (radians)
  static const float STAGGER;       // perpendicular endpoint offset (units)
  static const float FADE_MS;       // fade-out duration once fully grown

  ShockBolt(WrappedPoint origin, Point facing_dir, Object *owner = nullptr);
  void step_bolt(int delta, std::list<Object*> *asteroids, std::list<Object*> *hostiles);
  bool is_alive() const { return life > 0.0f; }

private:
  // Nearest live, un-avoided target in `lst` within SEEK_RANGE of `tip`.
  // Asteroids that are invincible are skipped (skip_invincible); hostiles are not.
  Object *seek_target(const Point &tip, std::list<Object*> *lst, bool skip_invincible,
                      float &best_dist) const;
  void grow_segment(std::list<Object*> *asteroids, std::list<Object*> *hostiles);
};

namespace Weapon {
  class Shock : public Base {
  public:
    Shock(Ship *ship);
    ~Shock();

    void shoot(bool on = true) override;
    void step(int delta) override;
    bool is_automatic() const override { return true; }

    // Cooldown accessors: the net client rebuilds the weapon list with fresh
    // objects 10x/s from snapshots, and a fresh Shock (cooldown 0) fires the
    // instant it is re-armed — an extra bolt+sound every snapshot while the
    // trigger is held. net_apply_state preserves the live cooldown across the
    // rebuild (mirroring the Default gun).
    int  get_cooldown() const { return cooldown; }
    void set_cooldown(int ms) { cooldown = ms; }

  private:
    void try_fire();  // fire a bolt if off cooldown; handles empty ammo
    int cooldown;   // ms until the next bolt may be fired
    Mix_Chunk *shoot_sound = NULL, *empty_sound = NULL;
  };
}

#endif
