#ifndef SHOCK_PICKUP_H
#define SHOCK_PICKUP_H

#include "pickup.h"

class ShockPickup : public Pickup {
public:
  ShockPickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
