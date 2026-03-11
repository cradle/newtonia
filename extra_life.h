#ifndef EXTRA_LIFE_H
#define EXTRA_LIFE_H

#include "pickup.h"

class ExtraLife : public Pickup {
public:
  ExtraLife(WrappedPoint pos);
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
};

#endif
