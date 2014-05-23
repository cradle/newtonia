#include "mine.h"
#include "../particle.h"
#include "../ship.h"

namespace Weapon {
  Mine::Mine(Ship *ship) :
    Base(ship) {
      _name = "MINES";
      _ammo = 100;
      unlimited = false;

      mine_sound = Mix_LoadWAV("mine.wav");
      if(mine_sound == NULL) {
        std::cout << "Unable to load mine.wav (" << Mix_GetError() << ")" << std::endl;
      }

      empty_sound = Mix_LoadWAV("empty.wav");
      if(empty_sound == NULL) {
        std::cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << std::endl;
      }
  }

  Mine::~Mine() {
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
        ship->mines.push_back(Particle(ship->tail(),  ship->facing*-0.1 + ship->velocity*0.1, 30000.0));
        if(mine_sound != NULL) {
          Mix_PlayChannel(-1, mine_sound, 0);
        }
      }
    }
  }

  void Mine::step(int delta) {
  }
}
