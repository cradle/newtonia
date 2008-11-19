#include "bullet.h"
#include "point.h"

Bullet::Bullet(Point position, Point velocity) {
  this->position = WrappedPoint(position);
  this->velocity = Point(velocity);
}

void Bullet::step(float delta) {
  position += velocity * delta;
  position.wrap();
}

void Bullet::set_world_size(Point size) {
  position.set_boundaries(size);
}