#ifndef LANCE_PICKUP_H
#define LANCE_PICKUP_H

#include "pickup.h"

class LancePickup : public Pickup {
public:
  LancePickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
