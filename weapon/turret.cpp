#include "turret.h"
#include "../asset_path.h"
#include "../sound_cache.h"
#include "../ship.h"
#include "../asteroid.h"
#include <math.h>
#include <iostream>

const float TurretDrone::LIFETIME_MS   = 60000.0f;
const int   TurretDrone::SHOTS         = 30;
const float TurretDrone::RANGE         = 900.0f;
const float TurretDrone::FIRE_INTERVAL = 600.0f;
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
    : Object(pos, vel), aim(wrap_angle(initial_aim)), ms_left(LIFETIME_MS),
      shots_left(SHOTS), fire_cooldown(FIRE_INTERVAL), has_target(false) {
  radius = RADIUS;
  radius_squared = radius * radius;
}

Object *TurretDrone::seek_target(std::list<Object*> *asteroids,
                                 std::list<Object*> *hostiles,
                                 const Ship *owner, float &best_dist) const {
  Object *best = NULL;
  best_dist = RANGE;
  if (asteroids) {
    for (Object *o : *asteroids) {
      if (!o->alive) continue;
      // The list is statically asteroids (the owner's missile-asteroid
      // list). Skip what a bullet can't harm right now: invincible rocks
      // and phased ghosts (same exclusions as the shock bolt's seek).
      Asteroid *a = static_cast<Asteroid *>(o);
      if (a->invincible) continue;
      if (a->phasing && a->phased) continue;
      float d = position.distance_to(o->position);
      if (d < best_dist) { best_dist = d; best = o; }
    }
  }
  if (hostiles) {
    for (Object *o : *hostiles) {
      if (!o->alive || o == (const Object *)owner) continue;
      float d = position.distance_to(o->position);
      if (d < best_dist) { best_dist = d; best = o; }
    }
  }
  return best;
}

void TurretDrone::step_turret(int delta, std::list<Object*> *asteroids,
                              std::list<Object*> *hostiles, Ship *owner) {
  ms_left -= delta;
  Object::step(delta);
  if (fire_cooldown > 0.0f) fire_cooldown -= delta;

  float dist = RANGE;
  Object *target = seek_target(asteroids, hostiles, owner, dist);
  if (target == NULL) {
    // Nothing in range: slow idle sweep, so the barrel visibly lives.
    has_target = false;
    aim = wrap_angle(aim + IDLE_SPIN * delta);
    return;
  }

  has_target = true;
  // Bear on the target the short way round the wrap.
  Point self = position.closest_to(target->position);
  float want = atan2f(target->position.y() - self.y(),
                      target->position.x() - self.x());
  float diff = wrap_angle(want - aim);
  float max_turn = TURN_RATE * delta;
  if (diff >  max_turn) diff =  max_turn;
  if (diff < -max_turn) diff = -max_turn;
  aim = wrap_angle(aim + diff);

  // Fire only when the barrel actually points at it (the target is already
  // known to be in range — seek_target bounds on RANGE).
  if (fabsf(wrap_angle(want - aim)) <= AIM_TOLERANCE &&
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
