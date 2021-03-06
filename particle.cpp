#include "particle.h"
#include "point.h"

Particle::Particle(const Point position, const Point velocity, float ttl, float rotation_speed) :
  Object(position, velocity, rotation_speed), time_to_live(ttl), time_left(ttl) {
  // commonInit();
}

void Particle::step(int delta) {
  Object::step(delta);
  time_left -= delta;
}

float Particle::aliveness() const {
  return time_left / time_to_live;
}

bool Particle::is_alive() const {
  return time_left > 0;
}
