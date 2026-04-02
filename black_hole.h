#ifndef BLACK_HOLE_H
#define BLACK_HOLE_H

#include "object.h"
#include "mesh.h"
#include <list>

// A stationary gravitational hazard that pulls all nearby objects toward it.
// Objects that cross the event horizon (radius) are destroyed.
class BlackHole : public Object {
public:
  BlackHole(WrappedPoint position);
  virtual ~BlackHole() {};

  virtual void step(int delta);
  virtual bool is_removable() const;

  void draw(bool is_minimap) const;

  // Apply gravitational acceleration to another object.
  // Returns true if the object crossed the event horizon (caller should kill it).
  bool apply_gravity(Object &other, int delta) const;

  static const float gravitational_strength;
  static const float influence_radius;

  Mesh mesh_fill;
  Mesh mesh_map_fill;
  Mesh mesh_map_ring;
};

#endif
