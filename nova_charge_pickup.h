#ifndef NOVA_CHARGE_PICKUP_H
#define NOVA_CHARGE_PICKUP_H

#include "pickup.h"

class NovaChargePickup : public Pickup {
public:
  NovaChargePickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
