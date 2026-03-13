#ifndef MISSILE_PICKUP_H
#define MISSILE_PICKUP_H

#include "pickup.h"

class MissilePickup : public Pickup {
public:
  MissilePickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
