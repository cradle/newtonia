#include "mine.h"
#include "../particle.h"
#include "../ship.h"

namespace Weapon {
  Mine::Mine(Ship *ship) : 
    Base(ship) {
      _name = "Mines";
      _ammo = 10;
      unlimited = false;
  }
  
  Mine::~Mine() {
  }
  
  void Mine::shoot(bool on) {
    if(on && _ammo > 0) {
      ship->mines.push_back(Particle(ship->tail(),  ship->facing*-0.1 + ship->velocity*0.1, 30000.0));
      _ammo--;
    }
  }
  
  void Mine::step(int delta) {
  }
}