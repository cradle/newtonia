#include "mine.h"
#include "../asset_path.h"
#include "../particle.h"
#include "../ship.h"

namespace Weapon {
  Mine::Mine(Ship *ship) :
    Base(ship) {
      _name = "MINES";
      _ammo = 10;
      unlimited = false;

      mine_sound = Mix_LoadWAV(asset_path("audio/mine.wav").c_str());
      if(mine_sound == NULL) {
        std::cout << "Unable to load mine.wav (" << Mix_GetError() << ")" << std::endl;
      }

      empty_sound = Mix_LoadWAV(asset_path("audio/empty.wav").c_str());
      if(empty_sound == NULL) {
        std::cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << std::endl;
      }
  }

  Mine::~Mine() {
    if(mine_sound != NULL) Mix_FreeChunk(mine_sound);
    if(empty_sound != NULL) Mix_FreeChunk(empty_sound);
  }

  void Mine::shoot(bool on) {
    if(on) {
      if(_ammo == 0) {
        if(empty_sound != NULL) {
          Mix_PlayChannel(-1, empty_sound, 0);
        }
        return;
      } else {
        _ammo--;
        ship->mines.push_back(Particle(ship->tail(),  ship->facing*-0.1 + ship->velocity*0.1, 30000.0, 0.2f));
        if(mine_sound != NULL) {
          Mix_PlayChannel(-1, mine_sound, 0);
        }
      }
    }
  }

  void Mine::step(int delta) {
  }
}
