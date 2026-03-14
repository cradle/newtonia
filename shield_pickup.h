#ifndef SHIELD_PICKUP_H
#define SHIELD_PICKUP_H

#include "pickup.h"

class ShieldPickup : public Pickup {
public:
  ShieldPickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
