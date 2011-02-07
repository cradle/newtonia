#include "default.h"
#include "../particle.h"
#include "../point.h"
#include "../ship.h"

#include <math.h>
#include <sstream>
#include <iostream>
#include <cstdlib>
using namespace std;

namespace Weapon {
  Default::Default(Ship *ship, bool automatic, int level, float accuracy, int time_between_shots) :
    Base(ship),
    automatic(automatic),
    time_until_next_shot(0),
    time_between_shots(time_between_shots),
    level(level),
    accuracy(accuracy) {
      stringstream temp_name;
      temp_name << "PP GUN ";
      if(level > 0) {
        temp_name << (level+1);
      }
      if(automatic) {
        temp_name << "A";
      }
      _name = temp_name.str();

      unlimited = (level == 0 && !automatic);

      if(!unlimited)
        _ammo = 100;
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
    	  fire();
    	  time_until_next_shot += time_between_shots;
    	}
    }
  }

  void Default::fire() {
    if(!unlimited) {
      if(_ammo == 0)
        return;
      else
        _ammo--;
    }
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
    }
  }

  void Default::fire_shot(Point direction) {
    direction = Point(direction);
    direction.rotate((rand() / (float)RAND_MAX) * accuracy - accuracy / 2.0);
    ship->bullets.push_back(Particle(ship->gun(), direction*0.615 + ship->velocity*0.99, 2600.0));
    if(!automatic) {
      shoot(false);
    }
  }
}
