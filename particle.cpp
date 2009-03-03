#include "particle.h"
#include "point.h"

Particle::Particle(const Point position, const Point velocity, float ttl) :
  Object(position, velocity), time_to_live(ttl), time_left(ttl) {
  // commonInit();
}

void Particle::step(float delta) {
  Object::step(delta);
  time_left -= delta;
}

float Particle::aliveness() const {
  return time_left / time_to_live;
}

bool Particle::is_alive() const {
  return time_left > 0;
}