#include "composite_object.h"
#include <math.h>
#include <cstdlib>

void CompositeObject::step(int delta) {
  Object::step(delta);
  std::list<Particle>::iterator deb = debris.begin();
  while(deb != debris.end()) {
    deb->step(delta);
    if(!deb->is_alive()) {
      deb = debris.erase(deb);
    } else {
      deb++;
    }
  }
}

bool CompositeObject::is_removable() const {
  return Object::is_removable() && debris.empty();
}

bool CompositeObject::kill() {
  if(Object::kill()) {
    explode();
    return true;
  }
  return false;
}

void CompositeObject::explode() {
  explode(position, velocity);
}

void CompositeObject::explode(Point position, Point velocity) {
  Point dir = (Point(0.3,0) * radius);
  for(int i = rand()%60+20; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    debris.push_back(Particle(position, velocity + dir*0.00005*(rand()%300 - 150), rand()%3000));
  }
}
