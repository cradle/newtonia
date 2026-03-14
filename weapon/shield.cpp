#include "shield.h"
#include "../ship.h"
#include <iostream>

namespace Weapon {
  Shield::Shield(Ship *ship) : Base(ship) {
    _name = "SHIELD";
    _ammo = 10;
    unlimited = false;

    empty_sound = Mix_LoadWAV("empty.wav");
    if(empty_sound == NULL) {
      std::cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }

  Shield::~Shield() {
  }

  void Shield::shoot(bool on) {
    if(on) {
      if(_ammo == 0) {
        if(empty_sound != NULL) {
          Mix_PlayChannel(-1, empty_sound, 0);
        }
        return;
      }
      _ammo--;
      ship->time_left_invincible += 1000;
      ship->invincible = true;
    }
  }

  void Shield::step(int delta) {
  }
}
