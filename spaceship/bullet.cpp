#include "bullet.h"
#include "point.h"

Bullet::Bullet(Point position, Point direction) {
  this->position = position;
  velocity = direction;
}

void Bullet::step(float delta) {
  position += velocity * delta;
}