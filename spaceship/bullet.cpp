#include "bullet.h"
#include "point.h"

Bullet::Bullet(Point position, Point velocity, float ttl) {
  this->position = WrappedPoint(position);
  this->velocity = Point(velocity);
  //TODO: make width/height class variables, only need to be set once!
  time_left = time_to_live = ttl;
  end = false;
}

void Bullet::step(float delta) {
  position += velocity * delta;
  time_left -= delta;
  position.wrap();
}

bool Bullet::cross_boundary(Bullet* first, Bullet* other) {
  return WrappedPoint::cross_boundary(first->position, other->position) ||
    WrappedPoint::cross_boundary(other->position, first->position);
}

float Bullet::aliveness() {
  return time_left / time_to_live;
}

void Bullet::set_end(bool value) {
  end = value;
}

bool Bullet::is_end() {
  return end;
}

bool Bullet::is_alive() {
  return time_left > 0;
}