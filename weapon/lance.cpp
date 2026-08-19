#include "lance.h"
#include "../asset_path.h"
#include "../ship.h"
#include "../stats.h"
#include "../achievements.h"

#include <iostream>
using namespace std;

namespace Weapon {
  const float Lance::RANGE = 1500.0f;  // = beam BOLT_SPEED (1.5) * BOLT_TTL (1000)

  Lance::Lance(Ship *ship) :
    Base(ship),
    time_until_next_shot(0),
    time_between_shots(400) {
    _name = "LANCE";
    unlimited = false;
    _ammo = 0;

    shoot_sound = Mix_LoadWAV(asset_path("audio/lance.wav").c_str());
    if(shoot_sound == NULL) {
      cout << "Unable to load lance.wav (" << Mix_GetError() << ")" << endl;
    }
    empty_sound = Mix_LoadWAV(asset_path("audio/empty.wav").c_str());
    if(empty_sound == NULL) {
      cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << endl;
    }
  }

  Lance::~Lance() {
    Mix_FreeChunk(shoot_sound);
    Mix_FreeChunk(empty_sound);
  }

  void Lance::shoot(bool on) {
    shooting = on;
  }

  void Lance::step(int delta) {
    time_until_next_shot -= delta;
    if(shooting && time_until_next_shot <= 0) {
      fire();
      time_until_next_shot = time_between_shots;
    }
    // One pulse per trigger pull.
    shooting = false;
  }

  void Lance::fire() {
    // PROTO 18: like Default, the host's remote-player lance keeps its
    // ammo/cooldown bookkeeping but fires no pulse and plays no sound —
    // the owning client ray-marches its own pulse (kills arrive as
    // MSG_HIT claims, the flash as an MSG_LANCE report).
    bool sim_only = ship->net_remote_gun;
    if(_ammo == 0) {
      if(empty_sound != NULL && !sim_only) {
        Mix_PlayChannel(-1, empty_sound, 0);
      }
      return;
    }
    _ammo--;
    // Lifetime SHOTS FIRED — one per pulse, same gates as Default::fire.
    if (ship->is_local_player && !Achievements::unlocks_suppressed())
      Stats::add_shot();
    if(sim_only) return;
    if(shoot_sound != NULL && ship->sound_volume_scale > 0.0f) {
      Mix_VolumeChunk(shoot_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
      Mix_PlayChannel(-1, shoot_sound, 0);
    }
    // Ship::step executes the pulse this frame with the collision grid.
    ship->lance_pulse_pending = true;
  }
}
