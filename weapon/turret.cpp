#include "turret.h"
#include "../asset_path.h"
#include "../sound_cache.h"
#include "../seek.h"
#include "../grid.h"
#include "../ship.h"
#include "../asteroid.h"
#include "../net_protocol.h"
#include <math.h>
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
      // where it is. Work in the turret's own frame — the mint gives each
      // bullet 0.99 of the turret's drift, so what matters is the target's
      // velocity RELATIVE to the turret — and solve |R + Vr*t| =
      // BULLET_SPEED*t for the earliest positive intercept time t (the
      // standard quadratic in t; both sides are units when t is ms).
      // c = |R|^2 > 0 always, so in the common a < 0 case (target slower
      // than a bullet) exactly one root is positive. No intercept — target
      // outrunning the bullet, or reachable only after the bullet's own
      // lifetime — falls back to pointing straight at it.
      Point self = position.closest_to(target->position);
      float rx = target->position.x() - self.x();
      float ry = target->position.y() - self.y();
      float vrx = target->velocity.x() - velocity.x() * 0.99f;
      float vry = target->velocity.y() - velocity.y() * 0.99f;
      float qa = vrx * vrx + vry * vry - BULLET_SPEED * BULLET_SPEED;
      float qb = 2.0f * (rx * vrx + ry * vry);
      float qc = rx * rx + ry * ry;
      float t_hit = -1.0f;
      if (fabsf(qa) < 1e-6f) {
        // Relative speed == bullet speed: the quadratic degenerates to
        // linear.
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
      float lead_x = rx, lead_y = ry;
      if (t_hit > 0.0f && t_hit <= BULLET_TTL_MS) {
        lead_x += vrx * t_hit;
        lead_y += vry * t_hit;
      }
      aim_want = atan2f(lead_y, lead_x);
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
