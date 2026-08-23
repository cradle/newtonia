#ifndef HAZARD_H
#define HAZARD_H

#include "object.h"
#include "mesh.h"
#include "particle.h"
#include "savegame.h"
#include <list>
#include <vector>

class GLShip;

// Environmental hazards introduced on the "quiet" mid-game generations that
// would otherwise add nothing new — generations 9, 11 and 12 (displayed levels
// 10, 12 and 13). Like the black hole these are non-scoring obstacles that
// shape how the player moves through the level rather than things to farm for
// points; the SEEKER is the one exception, paying a small flat bounty when shot
// down (no achievements, mirroring the black hole's hands-off relationship with
// the earn system).
//
// One class, three behaviours selected by `kind` — the same flag-selects-
// behaviour approach the Asteroid class uses:
//   PULSAR  stationary; emits an expanding shockwave ring on a fixed cycle that
//           hurls any ship the wavefront reaches outward, killing it unless it
//           is invincible (respawn shield / god mode).
//   COMET   an indestructible body that cruises in a straight line, wrapping the
//           world and trailing debris; lethal to any ship it strikes.
//   SEEKER  a small drone that homes on the nearest player and rams it; dies to
//           a single shot (worth SEEKER_REWARD) or on impact.
//   HUNTER  the seeker's late-game elite (generation >= 18): bigger, takes
//           HUNTER_HEALTH shots, and steers at the player's INTERCEPT point
//           (the shared seek.h solver, like the turret and the aim ramp) so it
//           cuts corners instead of tail-chasing. Its speed stays under the
//           player's plain thrust per the locked late-game design rule — a
//           committed straight-line escape always wins; the threat is that
//           turning is no longer free.
class Hazard : public Object {
public:
  // HUNTER appended (savegame v22 / PROTO 28 gate the new wire value — the
  // record itself is unchanged, `health` already rides for the comet).
  enum Kind { PULSAR, COMET, SEEKER, HUNTER };

  // Spawns at a random position clear of the world centre (where the black hole
  // and the players' start sit). COMET/SEEKER get a starting velocity; the
  // PULSAR stays put.
  Hazard(Kind kind, const Point &world);
  virtual ~Hazard() {}

  // Advance animation and (for COMET/SEEKER) motion. `players` is only used by
  // the SEEKER to steer toward the nearest one; the others ignore it.
  void update(int delta, std::list<GLShip*> *players);

  virtual bool is_removable() const override;

  void draw(bool minimap) const;

  Kind kind_of() const { return kind_; }

  // PULSAR: true while the lethal shockwave is expanding this cycle.
  bool wave_active() const;
  // PULSAR: the radius the wavefront has reached (0 while charging).
  float wave_radius() const { return wave_radius_; }

  // COMET / PULSAR / HUNTER: take one point of damage; spawns impact debris
  // and, at zero health, destroys the hazard. No effect on the seeker (it dies
  // in one shot via destroy()).
  void hit();

  // Spawn a death burst and mark the hazard destroyed (PULSAR never dies).
  void destroy();

  // ── Tuning (referenced by GLGame's collision code) ─────────────────────────
  static const float WAVE_BAND;    // PULSAR: half-thickness of the lethal front
  static const float KNOCKBACK;    // PULSAR: outward speed imparted to a ship
  static const int   SEEKER_REWARD;
  static const int   COMET_REWARD;
  static const int   PULSAR_REWARD;
  static const int   HUNTER_REWARD;

  Save::Hazard capture_state() const;
  static Hazard *from_state(const Save::Hazard &s, const Point &world);
  // Net client: reconcile a live replica to an authoritative snapshot in place
  // (pose/velocity/cycle timer/health), keeping meshes, debris and trail
  // continuity so motion stays smooth between the 10 Hz applies.
  void apply_net_state(const Save::Hazard &s);

private:
  void build_meshes();

  Kind  kind_;
  float timer_ = 0.0f;        // ms; PULSAR cycle phase / SEEKER blink phase
  float wave_radius_ = 0.0f;  // PULSAR only
  int   health_ = 0;          // COMET/PULSAR: shots remaining before it breaks up

  std::vector<Particle> debris_;   // SEEKER death burst / COMET trail
  int   trail_timer_ = 0;          // COMET: ms until the next trail puff

  Mesh body_mesh_;   // world-space body (PULSAR: core + accretion ring)
  Mesh beam_mesh_;   // PULSAR: rotating lighthouse beams
  Mesh ring_mesh_;   // PULSAR: unit circle scaled to the shockwave radius
  Mesh map_mesh_;    // minimap blip

  // Tuning that stays internal to hazard.cpp.
  static const float PULSAR_CHARGE_MS;
  static const float PULSAR_EXPAND_MS;
  static const float PULSAR_MAX_RADIUS;
  static const float PULSAR_CORE_RADIUS;
  static const int   PULSAR_HEALTH;
  static const float COMET_SPEED;
  static const int   COMET_HEALTH;
  static const float SEEKER_SPEED;
  static const float HUNTER_SPEED;
  static const int   HUNTER_HEALTH;
};

#endif
