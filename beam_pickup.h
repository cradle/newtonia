#ifndef BEAM_PICKUP_H
#define BEAM_PICKUP_H

#include "pickup.h"

class BeamPickup : public Pickup {
public:
  BeamPickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
