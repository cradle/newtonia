#include "giga_mine.h"
#include "../asset_path.h"
#include "../sound_cache.h"
#include "../particle.h"
#include "../ship.h"

namespace Weapon {
  GigaMine::GigaMine(Ship *ship) :
    Base(ship) {
      _name = "GIGA-MINE";
      _ammo = 1;
      unlimited = false;

      mine_sound = load_wav_cached("audio/mine.wav");
      if(mine_sound == NULL) {
        std::cout << "Unable to load mine.wav (" << Mix_GetError() << ")" << std::endl;
      }

      empty_sound = load_wav_cached("audio/empty.wav");
      if(empty_sound == NULL) {
        std::cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << std::endl;
      }
  }

  GigaMine::~GigaMine() {
    if(mine_sound != NULL) Mix_FreeChunk(mine_sound);
    if(empty_sound != NULL) Mix_FreeChunk(empty_sound);
  }

  void GigaMine::shoot(bool on) {
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
        // Drop giga-mine at ship's tail; slow rotation, long TTL like regular mine
        ship->giga_mines.push_back(Particle(ship->tail(), ship->facing*-0.1 + ship->velocity*0.1, 30000.0, 0.15f));
        // Deploy click: the ship's own cue, scaled like every gun play
        // site (full in your cockpit, faded for the far co-op partner).
        if(mine_sound != NULL && ship->sound_volume_scale > 0.0f) {
          Mix_VolumeChunk(mine_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
          Mix_PlayChannel(-1, mine_sound, 0);
        }
      }
    }
  }

  void GigaMine::step(int delta) {
  }
}
