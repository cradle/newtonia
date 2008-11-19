#ifndef BULLET_H
#define BULLET_H

#include "point.h"
#include "wrapped_point.h"

class Bullet {
public:
  Bullet() {};
  Bullet(Point position, Point direction);
  void step(float delta);
  void set_world_size(Point size);

//TODO: Friends
  WrappedPoint position;

private:
  Point velocity;
};

#endif /* BULLET_H */