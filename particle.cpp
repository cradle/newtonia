#include "particle.h"
#include "point.h"

Particle::Particle(const Point position, const Point velocity, float ttl) :
  position(WrappedPoint(position)), velocity(Point(velocity)), time_to_live(ttl), time_left(ttl) {
}

void Particle::step(float delta) {
  position += velocity * delta;
  time_left -= delta;
  position.wrap();
}

float Particle::aliveness() const {
  return time_left / time_to_live;
}

bool Particle::is_alive() const {
  return time_left > 0;
}