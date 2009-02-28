#include "default.h"
#include "../particle.h"
#include "../point.h"
#include "../ship.h"

#include <iostream>
using namespace std;

namespace Weapon {
  Default::Default(Ship *ship) : 
    Base(ship),
    ship(ship), // FIX: TODO: why do I have to call this when it is in base?
    shooting(false), 
    automatic(false),
    time_until_next_shot(0),
    time_between_shots(100),
    accuracy(0.1) {
  }

  Default::~Default() {
  }

  void Default::shoot(bool on) {
    if (on && time_until_next_shot < 0){
  	time_until_next_shot = 0;
    }
    shooting = on;
  }

  void Default::step(int delta) {
    time_until_next_shot -= delta;
    if(shooting) {
    	while(time_until_next_shot <= 0) {
    	  fire_shot();
    	  time_until_next_shot += time_between_shots;
    	}
    }
  }

  void Default::fire_shot() {
    Point dir = Point(ship->facing);
    dir.rotate((rand() / (float)RAND_MAX) * accuracy - accuracy / 2.0);
    ship->bullets.push_back(Particle(ship->gun(), dir*0.615 + ship->velocity*0.99, 2600.0));
    if(!automatic) {
  	shoot(false);
    }
  }
}
