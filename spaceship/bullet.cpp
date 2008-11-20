#include "bullet.h"
#include "point.h"

Bullet::Bullet(Point position, Point velocity, Point world_size, float ttl) {
  this->position = WrappedPoint(position);
  this->velocity = Point(velocity);
  //TODO: make width/height class variables, only need to be set once!
  set_world_size(world_size);
  time_left = ttl;
}

void Bullet::step(float delta) {
  position += velocity * delta;
  time_left -= delta;
  position.wrap();
}

void Bullet::set_world_size(Point size) {
  position.set_boundaries(size);
}

bool Bullet::is_alive() {
  return time_left > 0;
}