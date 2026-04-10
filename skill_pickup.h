#ifndef SKILL_PICKUP_H
#define SKILL_PICKUP_H

#include "pickup.h"
#include "mesh.h"

class SkillPickup : public Pickup {
public:
  SkillPickup(WrappedPoint pos);
  ~SkillPickup() {}
  void draw(float world_rotation = 0.0f) const;
  void apply(Ship *ship);
private:
  Mesh glow_mesh;
};

#endif
