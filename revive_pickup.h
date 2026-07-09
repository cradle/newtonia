#ifndef REVIVE_PICKUP_H
#define REVIVE_PICKUP_H

#include "pickup.h"

// Co-op only: drops (50%, one in the world at a time) while a partner is
// fully out of lives. Collecting it brings the fallen partner back on their
// last life — the revive itself is applied by GLGame at the collection site
// (the pickup can't see the player list), so apply() here is a no-op.
class RevivePickup : public Pickup {
public:
  RevivePickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
