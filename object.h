#ifndef OBJECT_H
#define OBJECT_H

#include "wrapped_point.h"

class Object {
public:
  Object();
  Object(WrappedPoint position, Point velocity);
  virtual void step(int delta);
  bool collide(Object *other);
  bool is_removable() const;
  
  //TODO: work out friend methods ::draw
  //TODO: work out inheritance with static, OR, work out borg?
  friend class AsteroidDrawer;
  
protected:
  float radius, radius_squared;
  WrappedPoint position;
  Point velocity;
  float rotation, rotation_speed;
  bool alive;
};

#endif