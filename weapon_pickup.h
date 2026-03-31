#ifndef WEAPON_PICKUP_H
#define WEAPON_PICKUP_H

#include "pickup.h"

class WeaponPickup : public Pickup {
public:
  WeaponPickup(WrappedPoint pos, int weapon_index);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
  int get_weapon_index() const { return weapon_index; }
private:
  int weapon_index;
};

#endif
