#ifndef TURRET_PICKUP_H
#define TURRET_PICKUP_H

#include "pickup.h"

// Grants 3 deployable sentry drones (weapon/turret.h). Teal ring with a
// barrel stub — the deployed drone's silhouette.
class TurretPickup : public Pickup {
public:
  TurretPickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
