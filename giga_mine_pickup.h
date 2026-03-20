#ifndef GIGA_MINE_PICKUP_H
#define GIGA_MINE_PICKUP_H

#include "pickup.h"

class GigaMinePickup : public Pickup {
public:
  GigaMinePickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
