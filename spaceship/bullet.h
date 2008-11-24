#ifndef BULLET_H
#define BULLET_H

#include "point.h"
#include "wrapped_point.h"

class Bullet {
public:
  Bullet() {};
  Bullet(Point position, Point direction, float time_to_live);
  void step(float delta);
  bool is_alive();
  float aliveness();
  void set_end(bool value = true);
  bool is_end();

//TODO: Friends
  WrappedPoint position;
  Point velocity;

private:
  bool end;
  float time_to_live;
  float time_left;
};

#endif /* BULLET_H */