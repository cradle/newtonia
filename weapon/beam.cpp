#include "beam.h"
#include "../asset_path.h"
#include "../particle.h"
#include "../point.h"
#include "../ship.h"
#include "../stats.h"
#include "../achievements.h"

#include <iostream>
using namespace std;

namespace Weapon {
  // Bolt travels ~2.5x faster than the default gun so it reads as a lance and
  // clears a line of asteroids in a couple of frames. The swept
  // segment-polygon collision in Ship handles the high speed without tunnelling.
  static const float BOLT_SPEED = 1.5f;
  static const float BOLT_TTL   = 1000.0f;

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
    // PROTO 18: like Default, the host's remote-player beam keeps its
    // ammo/cooldown bookkeeping but mints no bolt and plays no sound —
    // the real bolt arrives as an MSG_SHOT report (piercing flag set).
    bool sim_only = ship->net_remote_gun;
    if(_ammo == 0) {
      if(empty_sound != NULL && !sim_only) {
        Mix_PlayChannel(-1, empty_sound, 0);
      }
      return;
    }
    _ammo--;
    // Lifetime SHOTS FIRED — one per bolt, same gates as Default::fire.
    if (ship->is_local_player && !sim_only &&
        !Achievements::unlocks_suppressed())
      Stats::add_shot();
    if(sim_only) return;
    if(shoot_sound != NULL && ship->sound_volume_scale > 0.0f) {
      Mix_VolumeChunk(shoot_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
      Mix_PlayChannel(-1, shoot_sound, 0);
    }
    // Replay recorder: beam fire cue (kind 1 = beam.wav on playback).
    Ship::replay_beam_pews.push_back(
        Point(ship->position.x(), ship->position.y()));

    Point dir = Point(ship->facing);
    ship->bullets.push_back(Particle(ship->gun(), dir * BOLT_SPEED + ship->velocity * 0.99, BOLT_TTL));
    ship->mark_last_bullet_piercing();
    if(ship->god_mode_time_remaining() > 0) {
      ship->mark_last_bullet_trail();
      ship->mark_last_bullet_kills_invincible();
    }
    // Report after every mark so the peer's clone carries them all
    // (no-op unless net_report_shots — offline play is untouched).
    ship->net_report_last_bullet();
  }
}
