#ifndef ASTEROID_H
#define ASTEROID_H

#include "object.h"
#include "asteroid_drawer.h"

class Asteroid : public Object {
public:
  Asteroid();

  friend class AsteroidDrawer;

private:
  const static int max_speed;
};

#endif