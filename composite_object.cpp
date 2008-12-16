#include "composite_object.h"
#include <math.h>

void CompositeObject::step(float delta) {
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

void CompositeObject::explode() {
  explode(position, velocity);
}

void CompositeObject::explode(Point position, Point velocity) {
  Point dir = (Point(1,0) * radius * 1.2);
  for(int i = rand()%60+20; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    debris.push_back(Particle(position + dir, velocity + dir*0.000025*(rand()%300), rand()%3000));
  }  
}