#include "missile.h"
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
const float MissileShot::TURN_RATE     = 0.24f;   // degrees per ms
const int   MissileShot::TRAIL_LENGTH  = 20;

MissileShot::MissileShot(WrappedPoint pos, Point facing_dir, Point bv)
  : Object(pos, bv + facing_dir * INITIAL_SPEED),
    facing(facing_dir),
    thrust(0.0f),
    time_left(TIME_TO_LIVE)
{
  radius = 3.0f;
}

void MissileShot::step_missile(int delta, std::list<Object*> *asteroids, std::list<Object*> *ships) {
  time_left -= delta;

  // Seek nearest target (asteroid or ship) within forward cone
  static const float FORWARD_FOV = 45.0f;  // only seek targets within ±45° ahead
  {
    Object *target = NULL;
    float closest = SEEK_RANGE;

    auto seek_list = [&](std::list<Object*> *lst, bool skip_invincible) {
      if (!lst) return;
      for (auto it = lst->begin(); it != lst->end(); ++it) {
        Object *a = *it;
        if (!a->alive) continue;
        if (skip_invincible && a->invincible) continue;
        WrappedPoint apos = a->position;
        float dist = position.distance_to(apos) - a->radius;
        if (dist >= closest) continue;

        Point toward = (position.closest_to(apos) - apos) * -1.0f;
        float angle_to = facing.direction() - toward.normalized().direction();
        while (angle_to >  180.0f) angle_to -= 360.0f;
        while (angle_to < -180.0f) angle_to += 360.0f;
        if (std::fabs(angle_to) > FORWARD_FOV) continue;

        closest = dist;
        target = a;
      }
    };

    seek_list(asteroids, true);
    seek_list(ships, false);

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

  fly_sound = Mix_LoadWAV("audio/missile_fly.wav");
  if (fly_sound == NULL) {
    std::cout << "Unable to load missile_fly.wav (" << Mix_GetError() << ")" << std::endl;
  }

  empty_sound = Mix_LoadWAV("audio/empty.wav");
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
    if (empty_sound) Mix_PlayChannel(-1, empty_sound, 0);
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
      sp = std::shared_ptr<int>(new int(ch), [](int *p) { Mix_HaltChannel(*p); delete p; });
      fly_channel_handle = sp;
    }
  }
  ship->missiles.back().sound_handle = sp;
}

void Missile::step(int delta) {}

} // namespace Weapon
