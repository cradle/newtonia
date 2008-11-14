#ifndef BULLET_H
#define BULLET_H

#include "point.h"

class Bullet {
public:
  Bullet() {};
  Bullet(Point position, Point direction);
  void step(float delta);

private:
  Point velocity;
  Point position;
};

#endif /* BULLET_H */