#include "default.h"
#include "../asset_path.h"
#include "../sound_cache.h"
#include "../net_protocol.h"  // NET_LOG
#include "../particle.h"
#include "../point.h"
#include "../ship.h"

#include <math.h>
#include <sstream>
#include <iostream>
#include <cstdlib>
using namespace std;

namespace Weapon {
  Default::Default(Ship *ship, bool automatic, int level, float accuracy, int time_between_shots, int weapon_index, int burst_count, int burst_interval) :
    Base(ship),
    automatic(automatic),
    accuracy(accuracy),
    time_until_next_shot(0),
    time_between_shots(time_between_shots),
    level(level),
    _weapon_index(weapon_index),
    burst_count(burst_count > 1 ? burst_count : 1),
    burst_interval(burst_interval),
    burst_shots_left(0) {
      stringstream temp_name;
      temp_name << "PEW PEW";
      if(level == 5) {
        temp_name << " SCATTER";
      } else if (level > 0) {
        temp_name << " " << (level+1);
      }
      if(accuracy > 0.1f) {
        temp_name << " LOOSE";
      } else if (accuracy > 0.0f && accuracy < 0.1f) {
        temp_name << " TIGHT";
      } else if (accuracy == 0.0f) {
        temp_name << " POINT";
      }
      if(time_between_shots < 100) {
        temp_name << " RAPID";
      } else if(time_between_shots > 100) {
        temp_name << " SLOW";
      }
      if(automatic) {
        temp_name << " AUTO";
      }
      if(this->burst_count > 1) {
        temp_name << " BURST";
      }
      _name = temp_name.str();

      unlimited = (weapon_index == -1);

      if(!unlimited)
        _ammo = 100;

      shoot_sound = load_wav_cached("audio/shoot.wav");
      if(shoot_sound == NULL) {
        cout << "Unable to load shoot.wav (" << Mix_GetError() << ")" << endl;
      }

      if(!unlimited) {
        empty_sound = load_wav_cached("audio/empty.wav");
        if(empty_sound == NULL) {
          cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << endl;
        }
      }
  }

  Default::~Default() {
    Mix_FreeChunk(shoot_sound);
    Mix_FreeChunk(empty_sound);
  }

  void Default::shoot(bool on) {
    if (on && time_until_next_shot < 0){
      time_until_next_shot = 0;
    }
    shooting = on;
  }

  void Default::step(int delta) {
    time_until_next_shot -= delta;
    // A started burst finishes on its own clock: the semi-auto disarm in
    // fire_shot() ends the trigger PULL, not the burst, and releasing the
    // trigger mid-burst must not cancel the shots already owed.
    while(burst_shots_left > 0 && time_until_next_shot <= 0) {
      burst_shots_left--;
      fire();
      time_until_next_shot += burst_shots_left > 0 ? burst_interval : time_between_shots;
    }
    if(shooting) {
    	while(shooting && time_until_next_shot <= 0) {
    	  if(burst_count > 1) burst_shots_left = burst_count - 1;
    	  fire();
    	  time_until_next_shot += burst_shots_left > 0 ? burst_interval : time_between_shots;
    	}
    }
  }

  void Default::fire() {
    // PROTO 14: the host's remote-player gun keeps its ammo/cooldown/
    // trigger bookkeeping but plays no sim sound — the real bullets (and
    // the shot sound) arrive as MSG_SHOT reports from the owning client.
    bool sim_only = ship->net_remote_gun;
    if(!unlimited) {
      if(_ammo == 0) {
        burst_shots_left = 0;  // dead trigger clicks once, not per burst shot
        if(empty_sound != NULL && !sim_only) {
          Mix_PlayChannel(-1, empty_sound, 0);
        }
        return;
      } else {
        _ammo--;
        if(shoot_sound != NULL && ship->sound_volume_scale > 0.0f && !sim_only) {
          Mix_VolumeChunk(shoot_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
          Mix_PlayChannel(-1, shoot_sound, 0);
        }
      }
    } else {
      if(shoot_sound != NULL && ship->sound_volume_scale > 0.0f && !sim_only) {
        Mix_VolumeChunk(shoot_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
        Mix_PlayChannel(-1, shoot_sound, 0);
      }
    }
    // Replay recorder: one pew cue per trigger pull (all variants, players
    // and enemies alike); playback re-attenuates against its own camera.
    if (!sim_only)
      Ship::replay_pews.push_back(
          Point(ship->position.x(), ship->position.y()));
    Point dir = Point(ship->facing);
    switch(level) {
      case(0):
        fire_shot(dir);
        break;
      case(1):
        dir.rotate(0.2);
        fire_shot(dir);
        dir.rotate(-0.4);
        fire_shot(dir);
        break;
      case(2):
        fire_shot(dir);
        dir.rotate(-0.3);
        fire_shot(dir);
        dir.rotate(0.6);
        fire_shot(dir);
        break;
      case(3):
        fire_shot(dir);
        dir.rotate(-0.3);
        fire_shot(dir);
        dir.rotate(0.6);
        fire_shot(dir);
        dir.rotate(M_PI - 0.3);
        fire_shot(dir);
        break;
      case(4):
        fire_shot(dir);
        dir.rotate(-0.3);
        fire_shot(dir);
        dir.rotate(0.6);
        fire_shot(dir);
        dir.rotate(M_PI/2.0f - 0.3);
        fire_shot(dir);
        dir.rotate(-M_PI);
        fire_shot(dir);
        break;
      case(5):
        fire_shot(dir);
        dir.rotate(-0.1);
        fire_shot(dir);
        dir.rotate(-0.1);
        fire_shot(dir);
        dir.rotate(0.3);
        fire_shot(dir);
        dir.rotate(0.1);
        fire_shot(dir);
        break;
    }
  }

  void Default::fire_shot(Point direction) {
    // PROTO 14: the host's remote-player gun mints no bullets — its real
    // shots arrive as MSG_SHOT reports (exact clones). The semi-auto
    // disarm below still runs so the trigger bookkeeping stays in step.
    if(!ship->net_remote_gun) {
      direction = Point(direction);
      direction.rotate((rand() / (float)RAND_MAX) * accuracy - accuracy / 2.0);
      ship->bullets.push_back(Particle(ship->gun(), direction*0.615 + ship->velocity*0.99, 2000.0));
      if(ship->god_mode_time_remaining() > 0) {
        ship->mark_last_bullet_trail();
        ship->mark_last_bullet_kills_invincible();
      }
      // The owning client reports each bullet (multi-shot levels report
      // one per barrel) so the host spawns exact clones.
      ship->net_report_last_bullet();
    }
    if(!automatic) {
      shoot(false);
    }
  }
}
