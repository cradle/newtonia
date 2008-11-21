#include "bullet.h"
#include "point.h"

Bullet::Bullet(Point position, Point velocity, float ttl) {
  this->position = WrappedPoint(position);
  this->velocity = Point(velocity);
  time_left = time_to_live = ttl;
  end = false;
}

void Bullet::step(float delta) {
  position += velocity * delta;
  time_left -= delta;
  position.wrap();
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