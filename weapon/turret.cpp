#include "turret.h"
#include "../asset_path.h"
#include "../sound_cache.h"
#include "../seek.h"
#include "../grid.h"
#include "../ship.h"
#include "../asteroid.h"
#include "../net_protocol.h"
#include "../net_session.h"
#include <math.h>
#include <cmath>
#include <iostream>
#include <vector>

const float TurretDrone::LIFETIME_MS   = 60000.0f;
const int   TurretDrone::SHOTS         = 30;
const float TurretDrone::RANGE         = 900.0f;
const float TurretDrone::BULLET_SPEED  = 0.615f;   // the gun's bullet speed
const float TurretDrone::BULLET_TTL_MS = 2000.0f;  // the gun's bullet TTL
const float TurretDrone::FIRE_INTERVAL = 600.0f;
const float TurretDrone::SEEK_INTERVAL = 48.0f;   // ~20 re-seeks/s
const float TurretDrone::TURN_RATE     = 0.004f;   // ~230 deg/s tracking
const float TurretDrone::IDLE_SPIN     = 0.0006f;  // ~34 deg/s idle sweep
const float TurretDrone::AIM_TOLERANCE = 0.12f;
const float TurretDrone::RADIUS        = 14.0f;
const float TurretDrone::BARREL_LEN    = 22.0f;

// Smallest signed angle equivalent, for barrel-vs-bearing differences.
static float wrap_angle(float a) {
  while (a >  (float)M_PI) a -= 2.0f * (float)M_PI;
  while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
  return a;
}

bool TurretDrone::fields_sane(float x, float y, float vx, float vy, float aim,
                              float ms, float cooldown, uint8_t shots) {
  // aim needs a MAGNITUDE bound, not just isfinite: the constructor feeds it
  // through wrap_angle's subtract-2-pi loop, and above ~2.7e8 a float's ulp
  // exceeds 2*pi so the subtraction rounds back to the same value and the
  // loop never exits — a finite hostile aim was a hard hang, worse than the
  // WrappedPoint spin this predicate exists to stop. Every writer serializes
  // an aim already wrapped to [-pi, pi], so one turn of slack rejects
  // nothing real. ms is bounded below too: live drones always save with
  // ms_left > 0 (expiry sweeps before capture), and a negative record would
  // rebuild as pre-expired — up to 256 debris bursts on the first tick
  // after a hostile load.
  return net_coord_sane(x) && net_coord_sane(y) &&
         net_vel_sane(vx) && net_vel_sane(vy) &&
         std::isfinite(aim) && fabsf(aim) <= 2.0f * (float)M_PI &&
         std::isfinite(cooldown) &&
         std::isfinite(ms) && ms > 0.0f && ms <= LIFETIME_MS &&
         shots <= SHOTS;
}

TurretDrone TurretDrone::from_fields(float x, float y, float vx, float vy,
                                     float aim, float ms, float cooldown,
                                     uint8_t shots) {
  TurretDrone t(WrappedPoint(x, y), Point(vx, vy), aim);
  t.ms_left = ms;
  t.fire_cooldown = cooldown;
  t.shots_left = shots;
  return t;
}

TurretDrone::TurretDrone(WrappedPoint pos, Point vel, float initial_aim)
    : Object(pos, vel), aim(wrap_angle(initial_aim)), aim_want(wrap_angle(initial_aim)),
      ms_left(LIFETIME_MS), shots_left(SHOTS), fire_cooldown(FIRE_INTERVAL),
      seek_wait(0.0f), has_target(false) {
  radius = RADIUS;
  radius_squared = radius * radius;
}

Object *TurretDrone::seek_target(const Grid *grid,
                                 std::list<Object*> *hostiles,
                                 const Ship *owner) const {
  float best_dist = RANGE;
  // Broad phase by the grid: only the cells the RANGE circle touches (the
  // grid only ever indexes the asteroid list, so every candidate is
  // statically an Asteroid). Reused scratch, not a per-seek allocation
  // (the game loop is single-threaded, like the renderer's static
  // MeshBuilders); duplicates from the broad phase are harmless to the
  // min scan. Skip what a bullet can't harm right now: invincible rocks
  // and phased ghosts (same exclusions as the shock bolt's seek).
  static std::vector<Object *> candidates;
  grid->query_radius(Point(position.x(), position.y()), RANGE, candidates);
  Object *best = seek_nearest(position, &candidates, best_dist,
      [](Object *o, const Point &, float) {
        Asteroid *a = static_cast<Asteroid *>(o);
        if (a->invincible) return false;
        if (a->phasing && a->phased) return false;
        return true;
      });
  Object *h = seek_nearest(position, hostiles, best_dist,
      [owner](Object *o, const Point &, float) {
        return o != (const Object *)owner;
      });
  if (h) best = h;
  return best;
}

void TurretDrone::step_turret(int delta, const Grid *grid,
                              std::list<Object*> *hostiles, Ship *owner) {
  ms_left -= delta;
  Object::step(delta);
  if (fire_cooldown > 0.0f) fire_cooldown -= delta;

  // Re-seek at SEEK_INTERVAL, not every step: the scan walks the whole
  // asteroid list, which at late generations costs more than everything
  // else this drone does put together. Between seeks the barrel keeps
  // turning toward the last computed bearing — no target POINTER survives
  // the gap (asteroids are deleted between ticks), only the angle. Worst
  // case a target dies inside the window and one shot chases a bearing
  // ~SEEK_INTERVAL stale; at FIRE_INTERVAL's rate that is rare and cheap.
  seek_wait -= delta;
  if (seek_wait <= 0.0f) {
    seek_wait = SEEK_INTERVAL;
    Object *target = seek_target(grid, hostiles, owner);
    has_target = target != NULL;
    if (target != NULL) {
      // LEAD the target: aim where it will be when the bullet lands, not
      // where it is — the shared intercept solve (seek.h), at full lead.
      Point self = position.closest_to(target->position);
      Point rel(target->position.x() - self.x(),
                target->position.y() - self.y());
      Point rel_vel(target->velocity.x() - velocity.x() * 0.99f,
                    target->velocity.y() - velocity.y() * 0.99f);
      Point off = intercept_offset(rel, rel_vel, BULLET_SPEED,
                                   BULLET_TTL_MS, 1.0f);
      aim_want = atan2f(rel.y() + off.y(), rel.x() + off.x());
    }
  }

  if (!has_target) {
    // Nothing in range: slow idle sweep, so the barrel visibly lives.
    aim = wrap_angle(aim + IDLE_SPIN * delta);
    return;
  }

  float diff = wrap_angle(aim_want - aim);
  float max_turn = TURN_RATE * delta;
  if (diff >  max_turn) diff =  max_turn;
  if (diff < -max_turn) diff = -max_turn;
  aim = wrap_angle(aim + diff);

  // Fire only when the barrel actually points at the lead (the target was
  // in range at the last seek — seek_target bounds on RANGE).
  if (fabsf(wrap_angle(aim_want - aim)) <= AIM_TOLERANCE &&
      fire_cooldown <= 0.0f && shots_left > 0)
    owner->fire_turret_bullet(*this);
}

namespace Weapon {
  Turret::Turret(Ship *ship) :
    Base(ship) {
      _name = "TURRET";
      _ammo = 3;
      unlimited = false;

      deploy_sound = load_wav_cached("audio/mine.wav");
      if(deploy_sound == NULL) {
        std::cout << "Unable to load mine.wav (" << Mix_GetError() << ")" << std::endl;
      }

      empty_sound = load_wav_cached("audio/empty.wav");
      if(empty_sound == NULL) {
        std::cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << std::endl;
      }
  }

  Turret::~Turret() {
    if(deploy_sound != NULL) Mix_FreeChunk(deploy_sound);
    if(empty_sound != NULL) Mix_FreeChunk(empty_sound);
  }

  void Turret::shoot(bool on) {
    if(on) {
      if(_ammo == 0) {
        if(empty_sound != NULL && ship->sound_volume_scale > 0.0f) {
          Mix_VolumeChunk(empty_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
          Mix_PlayChannel(-1, empty_sound, 0);
        }
        return;
      } else {
        _ammo--;
        stat_secondary_used(ship);
        // Deploy at the tail like a mine, drifting gently with the ship's
        // momentum; the barrel opens facing the way the ship faces.
        ship->turrets.push_back(TurretDrone(
            ship->tail(), ship->facing * -0.05f + ship->velocity * 0.1f,
            atan2f(ship->facing.y(), ship->facing.x())));
        // Deploys are otherwise silent in the logs; the e2e driver
        // (test/e2e/turret_net.sh) keys its selection probes on this line —
        // note it also fires on the HOST's replica of a client's deploy
        // (the INPUT press replays through this same weapon sim).
        NET_LOG("turret deployed at (%.0f, %.0f)\n",
                ship->turrets.back().position.x(),
                ship->turrets.back().position.y());
        if(deploy_sound != NULL && ship->sound_volume_scale > 0.0f) {
          Mix_VolumeChunk(deploy_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
          Mix_PlayChannel(-1, deploy_sound, 0);
        }
      }
    }
  }

  void Turret::step(int delta) {
  }
}
