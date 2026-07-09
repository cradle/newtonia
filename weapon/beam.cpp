#include "beam.h"
#include "../asset_path.h"
#include "../particle.h"
#include "../point.h"
#include "../ship.h"

#include <iostream>
using namespace std;

namespace Weapon {
  // Bolt travels ~2.5x faster than the default gun so it reads as a lance and
  // clears a line of asteroids in a couple of frames. The swept
  // segment-polygon collision in Ship handles the high speed without tunnelling.
  static const float BOLT_SPEED = 1.5f;
  static const float BOLT_TTL   = 2000.0f;

  Beam::Beam(Ship *ship) :
    Base(ship),
    time_until_next_shot(0),
    time_between_shots(220) {
    _name = "PIERCE BEAM";
    unlimited = false;
    _ammo = 0;

    shoot_sound = Mix_LoadWAV(asset_path("audio/beam.wav").c_str());
    if(shoot_sound == NULL) {
      cout << "Unable to load beam.wav (" << Mix_GetError() << ")" << endl;
    }
    empty_sound = Mix_LoadWAV(asset_path("audio/empty.wav").c_str());
    if(empty_sound == NULL) {
      cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << endl;
    }
  }

  Beam::~Beam() {
    Mix_FreeChunk(shoot_sound);
    Mix_FreeChunk(empty_sound);
  }

  void Beam::shoot(bool on) {
    shooting = on;
  }

  void Beam::step(int delta) {
    time_until_next_shot -= delta;
    if(shooting && time_until_next_shot <= 0) {
      fire();
      time_until_next_shot = time_between_shots;
    }
    // One bolt per trigger pull; releasing and pressing fire again shoots the next.
    shooting = false;
  }

  void Beam::fire() {
    if(_ammo == 0) {
      if(empty_sound != NULL) {
        Mix_PlayChannel(-1, empty_sound, 0);
      }
      return;
    }
    _ammo--;
    if(shoot_sound != NULL && ship->sound_volume_scale > 0.0f) {
      Mix_VolumeChunk(shoot_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
      Mix_PlayChannel(-1, shoot_sound, 0);
    }

    Point dir = Point(ship->facing);
    ship->bullets.push_back(Particle(ship->gun(), dir * BOLT_SPEED + ship->velocity * 0.99, BOLT_TTL));
    ship->mark_last_bullet_piercing();
    if(ship->god_mode_time_remaining() > 0) {
      ship->mark_last_bullet_trail();
      ship->mark_last_bullet_kills_invincible();
    }
  }
}
