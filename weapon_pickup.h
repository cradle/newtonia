#ifndef WEAPON_PICKUP_H
#define WEAPON_PICKUP_H

#include "object.h"

class WeaponPickup : public Object {
public:
  WeaponPickup(WrappedPoint pos, int weapon_index);
  void draw(float world_rotation = 0.0f) const;
  bool is_removable() const override;
  bool collected;
  int weapon_index;
};

#endif
