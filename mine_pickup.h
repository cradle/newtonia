#ifndef MINE_PICKUP_H
#define MINE_PICKUP_H

#include "pickup.h"

class MinePickup : public Pickup {
public:
  MinePickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
