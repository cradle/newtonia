#ifndef PICKUP_H
#define PICKUP_H

#include "object.h"

class Ship;

class Pickup : public Object {
public:
  Pickup(WrappedPoint pos) : collected(false) {
    position = pos;
    velocity = Point(0, 0);
    radius = 20.0f;
    rotation_speed = 0.0f;
    alive = true;
  }
  virtual ~Pickup() {}
  virtual void draw(float world_rotation = 0.0f) const = 0;
  virtual void apply(Ship *ship) = 0;
  bool is_removable() const override { return collected; }
  bool collected;
};

#endif
