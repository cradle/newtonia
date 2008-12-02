#ifndef PARTICLE_H
#define PARTICLE_H

#include "point.h"
#include "wrapped_point.h"

class Particle {
public:
  Particle() {};
  Particle(const Point position, const Point direction, float time_to_live);
  
  void step(float delta);
  bool is_alive() const;
  float aliveness() const;

  //TODO: Friends & glparticle refactor (i.e. draw(), explode())
  WrappedPoint position;
  Point velocity;
  
private:
  float time_to_live, time_left;
};

#endif /* PARTICLE_H */