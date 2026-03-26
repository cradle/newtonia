#ifndef GOD_MODE_PICKUP_H
#define GOD_MODE_PICKUP_H

#include "pickup.h"

class GodModePickup : public Pickup {
public:
  GodModePickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
