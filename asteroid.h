#ifndef ASTEROID_H
#define ASTEROID_H

#include "composite_object.h"
#include "asteroid_drawer.h"

class Asteroid : public CompositeObject {
public:
  Asteroid(bool invincible);
  Asteroid(Asteroid const *mother);
  virtual ~Asteroid();
  
  void add_children(list<Asteroid*> *objects);

  friend class AsteroidDrawer;
  
  static int num_killable;
  
  const static int max_radius;

private:
  const static int max_speed;
  const static int radius_variation;
  const static int minimum_radius;
  const static int max_rotation;
  
  bool children_added;
};

#endif