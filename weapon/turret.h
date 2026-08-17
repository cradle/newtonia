#ifndef TURRET_H
#define TURRET_H

#include "base.h"
#include "../object.h"
#include "../point.h"
#include "../wrapped_point.h"
#include <list>
#include <stdint.h>
#include <SDL_mixer.h>

class Ship;

// A deployed sentry drone: a player-coloured circle with a rotating barrel,
// dropped like a mine and left to fight on its own. It seeks the nearest
// killable target in RANGE (asteroids via the owner's missile-asteroid list,
// enemies/stations/hazards via shock_targets — the other player only under
// friendly fire), turns the barrel toward the INTERCEPT point — where the
// target will be when a bullet fired now arrives, the classic
// constant-velocity lead solved in the turret's own frame (its bullets
// inherit its drift) — at TURN_RATE, and fires an ordinary player bullet
// from the muzzle only once the barrel is actually pointing at that lead
// for a target in range. It retires after LIFETIME_MS or SHOTS
// bullets — whichever comes first — and blows apart into debris; it also
// dies to anything kill-aligned that touches it (asteroid contact, hostile
// bullets, a comet/seeker ram — those checks live in Ship::collide_grid and
// GLGame's hostile passes, which own the lists involved).
//
// Its bullets go into the OWNER's `bullets` vector through
// Ship::fire_turret_bullet, so every downstream system — asteroid collision
// and scoring, station/hazard damage, MSG_SHOT replication, FX_BULLET replay
// clones — treats them exactly like gun shots with zero extra wiring. The
// mint is gated the way the gun's is: replicated copies (the host's remote
// replica, a client's view of the peer, replay ghosts) run the same
// aim/cooldown/ammo bookkeeping so expiry stays in step, but only the
// locally-piloted ship's turret spawns the bullet and its sounds.
struct TurretDrone : public Object {
  float aim;             // barrel angle, radians (world frame)
  float ms_left;         // lifetime countdown, LIFETIME_MS at deploy
  int   shots_left;      // bullets remaining, SHOTS at deploy
  float fire_cooldown;   // ms until the next shot may fire
  bool  has_target;      // barrel currently tracking something in range
  // Net client, locally-deployed turret the host has not echoed back yet —
  // see Ship::NET_DEPLOY_GRACE (same contract as MissileShot's).
  uint8_t net_unconfirmed = 0;

  static const float LIFETIME_MS;    // 60 s on station, then it retires
  static const int   SHOTS;          // 30 bullets, whichever comes first
  static const float RANGE;          // fire only at targets inside this
  static const float BULLET_SPEED;   // muzzle speed, units/ms — ONE constant
                                     // shared with Ship::fire_turret_bullet's
                                     // mint, or the lead prediction and the
                                     // real bullet drift apart
  static const float BULLET_TTL_MS;  // bullet lifetime (mint's Particle TTL):
                                     // an intercept later than this can never
                                     // land, so the aim falls back to direct
  static const float FIRE_INTERVAL;  // ms between shots
  static const float TURN_RATE;      // barrel tracking rate, rad/ms
  static const float IDLE_SPIN;      // slow idle sweep with no target, rad/ms
  static const float AIM_TOLERANCE;  // fire once within this many rad of target
  static const float RADIUS;         // body circle radius (world units)
  static const float BARREL_LEN;     // barrel length == muzzle spawn offset

  TurretDrone(WrappedPoint pos, Point vel, float initial_aim);

  bool expired() const { return ms_left <= 0.0f || shots_left <= 0; }

  // One sim tick: drift, count down, seek, turn the barrel, and (through
  // owner->fire_turret_bullet) fire when aligned on an in-range target.
  void step_turret(int delta, std::list<Object*> *asteroids,
                   std::list<Object*> *hostiles, Ship *owner);

private:
  // Nearest live target within RANGE: killable asteroids (invincible rocks
  // and phased ghosts are skipped — a bullet can't harm them right now) plus
  // the hostiles list, never the owner.
  Object *seek_target(std::list<Object*> *asteroids,
                      std::list<Object*> *hostiles, const Ship *owner,
                      float &best_dist) const;
};

namespace Weapon {
  // The secondary that deploys TurretDrones. One drone per trigger pull,
  // like a mine; a pickup grants 3.
  class Turret : public Base {
  public:
    Turret(Ship *ship);
    ~Turret();

    void shoot(bool on = true) override;
    void step(int delta) override;

  private:
    Mix_Chunk *deploy_sound = NULL, *empty_sound = NULL;
  };
}

#endif
