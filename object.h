#ifndef OBJECT_H
#define OBJECT_H

#include "wrapped_point.h"
#include "drawer.h"

class Object {
public:
  Object();
  void step(int delta);
  bool collide(Object const other) const;
  
  //TODO: work out friend methods ::draw
  //TODO: work out inheritance with static, OR, work out borg?
  friend class AsteroidDrawer;
  
protected:
  int radius, radius_squared;
  WrappedPoint position;
  Point velocity;
};

#endif