#ifndef TIME_SLOW_PICKUP_H
#define TIME_SLOW_PICKUP_H

#include "pickup.h"

// Slows the whole world's wall-clock rate for a few seconds while the
// collector keeps their real turn rate — an aiming window, not a weapon.
// A world effect like the revive: GLGame applies it at the collection site
// (start_time_slow), since the pickup can't see the game's clock.
class TimeSlowPickup : public Pickup {
public:
  TimeSlowPickup(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
