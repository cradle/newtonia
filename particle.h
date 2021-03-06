#ifndef PARTICLE_H
#define PARTICLE_H

#include "object.h"
#include "point.h"
#include "wrapped_point.h"

class Particle : public Object {
public:
  Particle(const Point position, const Point direction, float time_to_live, float rotation_speed = 0.0f);
  virtual ~Particle() {};

  virtual void step(int delta);
  bool is_alive() const;
  float aliveness() const;

  //TODO: Fix encapsulation, GLShip -> ParticleDrawer etc.
  friend class GLShip;
  friend class Ship;
  friend class GLTrail;

private:
  float time_to_live, time_left;
};

#endif /* PARTICLE_H */
