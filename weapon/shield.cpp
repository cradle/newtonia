#include "shield.h"
#include "../ship.h"
#include "../shield_behaviour.h"
#include <iostream>

namespace Weapon {
  Shield::Shield(Ship *ship) : Base(ship) {
    _name = "SHIELD";
    _ammo = 10;
    unlimited = false;

    empty_sound = Mix_LoadWAV("audio/empty.wav");
    if(empty_sound == NULL) {
      std::cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }

  Shield::~Shield() {
    if(empty_sound != NULL) Mix_FreeChunk(empty_sound);
  }

  void Shield::shoot(bool on) {
    shooting = on;
    if(on) {
      if(ship->invincible) return;  // already active, don't consume ammo again
      if(_ammo == 0) {
        if(empty_sound != NULL) Mix_PlayChannel(-1, empty_sound, 0);
        return;
      }
      _ammo--;
      ship->add_behaviour(new ShieldBehaviour(ship, 1000));
    }
  }

  void Shield::step(int delta) {
    if(shooting && !ship->invincible) {
      if(_ammo == 0) {
        if(empty_sound != NULL) Mix_PlayChannel(-1, empty_sound, 0);
        return;
      }
      _ammo--;
      ship->add_behaviour(new ShieldBehaviour(ship, 1000));
    }
  }
}
