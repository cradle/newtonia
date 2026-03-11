#include "composite_object.h"
#include <math.h>
#include <cstdlib>

void CompositeObject::step(int delta) {
  Object::step(delta);
  for(size_t i = 0; i < debris.size(); ) {
    debris[i].step(delta);
    if(!debris[i].is_alive()) {
      debris[i] = std::move(debris.back());
      debris.pop_back();
    } else {
      ++i;
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
  for(int i = rand()%20+10; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    debris.push_back(Particle(position, velocity + dir*0.00005*(rand()%300 - 150), rand()%1500));
  }
}
