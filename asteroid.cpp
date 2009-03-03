#include "asteroid.h"
#include "wrapped_point.h"

#include <list>

using namespace std;

const int Asteroid::radius_variation = 285;
const int Asteroid::minimum_radius = 15.0f;
const int Asteroid::max_speed = 7;
const int Asteroid::max_rotation = 10;
int Asteroid::num_killable = 0;

Asteroid::Asteroid() : CompositeObject() {
  position = WrappedPoint();
  radius = rand()%radius_variation + minimum_radius;
  rotation_speed = (rand()%max_rotation-max_rotation/2)/radius;
  velocity = Point(rand()-RAND_MAX/2, rand()-RAND_MAX/2).normalized()*max_speed/radius;
  value = float(radius/(radius_variation + minimum_radius)) * 100.0f;
  children_added = false;
  invincible = (rand()/float(RAND_MAX)) > 0.8f;
  if(!invincible) {
    num_killable++;
  }
}

Asteroid::~Asteroid() {
  if(!invincible) {
    num_killable--;
  }
}

Asteroid::Asteroid(Asteroid const *mother) {
  radius = mother->radius/2.0f;
  rotation_speed = (rand()%6-3)/radius;
  velocity = Point(rand()-RAND_MAX/2, rand()-RAND_MAX/2).normalized()*max_speed/radius;
  position = mother->position + velocity.normalized() * radius;
  value = float(radius/(radius_variation + minimum_radius)) * 100.0f;
  value += mother->value;
  children_added = false;
  num_killable++;
}

void Asteroid::add_children(list<Asteroid*> *roids) {
  if(children_added) return;
  children_added = true;

  if(radius/2.0f < minimum_radius) {
    // explode good and proper
  } else {
    roids->push_back(new Asteroid(this));
    roids->push_back(new Asteroid(this));
  }

  velocity = velocity / 4;
}
