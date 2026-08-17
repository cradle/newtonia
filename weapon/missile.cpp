#include "missile.h"
#include "../seek.h"
#include "../world_sound.h"
#include "../asset_path.h"
#include "../sound_cache.h"
#include "../ship.h"
#include "../point.h"
#include "../wrapped_point.h"
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// MissileShot constants
const float MissileShot::TIME_TO_LIVE  = 3000.0f;
const float MissileShot::INITIAL_SPEED = 0.3f;
const float MissileShot::ACCELERATION  = 0.00015f;
const float MissileShot::MAX_THRUST    = 0.003f;    // acceleration units (units/ms²)
const float MissileShot::MAX_SPEED     = 0.8f;
const float MissileShot::SEEK_RANGE    = 700.0f;
const float MissileShot::TURN_RATE     = 0.36f;   // degrees per ms
const int   MissileShot::TRAIL_LENGTH  = 20;

MissileShot::MissileShot(WrappedPoint pos, Point facing_dir, Point bv)
  : Object(pos, bv + facing_dir * INITIAL_SPEED),
    facing(facing_dir),
    thrust(0.0f),
    time_left(TIME_TO_LIVE)
{
  radius = 3.0f;
}

void MissileShot::step_missile(int delta, std::list<Object*> *asteroids,
                               std::list<Object*> *ships, bool seek_players) {
  time_left -= delta;

  // Seek nearest target (asteroid or ship) within forward cone. The scan is
  // the shared seek_nearest loop (seek.h); the ±45° cone and the per-list
  // exclusions live in the filters. fwd is unit length, so the dot product
  // is |to_o|*cos(angle) and the cone keeps cos(angle) >= cos(45°) — the
  // same test the old degree arithmetic made.
  {
    Object *target = NULL;
    float closest = SEEK_RANGE;
    Point fwd = facing.normalized();
    static const float kCos45 = 0.70710678f;
    auto in_cone = [&fwd](const Point &to_o, float mag) {
      return to_o.x() * fwd.x() + to_o.y() * fwd.y() >= mag * kCos45;
    };

    Object *hit = seek_nearest(position, asteroids, closest,
        [&in_cone](Object *a, const Point &to_o, float mag) {
          if (a->invincible) return false;
          return in_cone(to_o, mag);
        });
    if (hit) target = hit;
    hit = seek_nearest(position, ships, closest,
        [&in_cone, seek_players](Object *a, const Point &to_o, float mag) {
          if (!seek_players) {
            Ship *s = dynamic_cast<Ship*>(a);
            if (s && s->player_ship) return false;
          }
          return in_cone(to_o, mag);
        });
    if (hit) target = hit;

    if (target) {
      WrappedPoint tpos = target->position;
      // Direction from missile toward asteroid
      Point toward = position.closest_to(tpos) - tpos;
      toward = toward * -1.0f;  // now points missile → asteroid
      float target_dir = toward.normalized().direction();
      float current_dir = facing.direction();
      float diff = current_dir - target_dir;
      // Normalise to -180..+180
      while (diff >  180.0f) diff -= 360.0f;
      while (diff < -180.0f) diff += 360.0f;

      float turn = TURN_RATE * (float)delta;
      if (std::fabs(diff) <= turn) {
        facing = toward.normalized();
      } else if (diff > 0.0f) {
        facing.rotate(-turn * (float)M_PI / 180.0f);  // turn right
      } else {
        facing.rotate( turn * (float)M_PI / 180.0f);  // turn left
      }
    }
  }

  // Thrust grows over time; facing gives its direction (same model as ship)
  thrust += ACCELERATION * (float)delta;
  if (thrust > MAX_THRUST) thrust = MAX_THRUST;
  velocity += facing * (thrust * (float)delta);
  float spd = velocity.magnitude();
  if (spd > MAX_SPEED) velocity = velocity * (MAX_SPEED / spd);

  // Update position (Object::step handles position += velocity*delta + wrap)
  Object::step(delta);

  // Record trail position
  trail.push_front(position);
  if ((int)trail.size() > TRAIL_LENGTH) trail.pop_back();
}

// ----------------------------------------------------------------------------

namespace Weapon {

Missile::Missile(Ship *ship) : Base(ship) {
  _name = "MISSILES";
  _ammo = 10;
  unlimited = false;

  fly_sound = load_wav_cached("audio/missile_fly.wav");
  if (fly_sound == NULL) {
    std::cout << "Unable to load missile_fly.wav (" << Mix_GetError() << ")" << std::endl;
  }

  empty_sound = load_wav_cached("audio/empty.wav");
  if (empty_sound == NULL) {
    std::cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << std::endl;
  }
}

Missile::~Missile() {
  if (fly_sound)   Mix_FreeChunk(fly_sound);
  if (empty_sound) Mix_FreeChunk(empty_sound);
}

void Missile::shoot(bool on) {
  if (!on) return;

  if (_ammo == 0) {
    if (empty_sound && ship->sound_volume_scale > 0.0f) {
      Mix_VolumeChunk(empty_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
      Mix_PlayChannel(-1, empty_sound, 0);
    }
    return;
  }

  _ammo--;
  ship->missiles.push_back(
    MissileShot(ship->gun(), ship->facing.normalized(), ship->velocity)
  );
  auto sp = fly_channel_handle.lock();
  if (!sp && fly_sound) {
    int ch = Mix_PlayChannel(-1, fly_sound, -1);
    if (ch != -1) {
      // Level from the launch point now, then per tick from the missiles
      // (Ship::update_missile_fly_volumes — a loop is chunk-independent
      // channel volume, and the distance behind it keeps moving); the
      // deleter restores the dynamic channel for its next tenant.
      Mix_Volume(ch, (int)(MIX_MAX_VOLUME * WorldSound::volume_at(ship->position)));
      sp = std::shared_ptr<int>(new int(ch), [](int *p) {
        Mix_HaltChannel(*p);
        Mix_Volume(*p, MIX_MAX_VOLUME);
        delete p;
      });
      fly_channel_handle = sp;
    }
  }
  ship->missiles.back().sound_handle = sp;
}

void Missile::step(int delta) {}

} // namespace Weapon
