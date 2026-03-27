#ifndef GOD_MODE_PICKUP_H
#define GOD_MODE_PICKUP_H

#include "pickup.h"

class GodModePickup : public Pickup {
public:
  GodModePickup(WrappedPoint pos);
  ~GodModePickup();
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
private:
  GLuint display_list;
};

#endif
