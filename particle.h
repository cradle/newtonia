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
  bool world_bullet = false;
  bool has_trail = false;
  bool kills_invincible = false;
  int trail_timer = 0;
};

#endif /* PARTICLE_H */
