#ifndef ASTEROID_H
#define ASTEROID_H

#include "composite_object.h"
#include "asteroid_drawer.h"

class Asteroid : public CompositeObject {
public:
  Asteroid();
  Asteroid(Asteroid const *mother);
  
  void add_children(list<Asteroid*> *objects);

  friend class AsteroidDrawer;

private:
  const static int max_speed;
  const static int radius_variation;
  const static int minimum_radius;
  const static int max_rotation;
  
  bool children_added;
};

#endif