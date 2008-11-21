#ifndef BULLET_H
#define BULLET_H

#include "point.h"
#include "wrapped_point.h"

class Bullet {
public:
  Bullet() {};
  Bullet(Point position, Point direction, Point world_size, float time_to_live);
  void set_world_size(Point size);
  void step(float delta);
  bool is_alive();
  float aliveness();
  void set_end(bool value = true);
  bool is_end();

//TODO: Friends
  WrappedPoint position;

private:
  bool end;
  Point velocity;
  float time_to_live;
  float time_left;
};

#endif /* BULLET_H */