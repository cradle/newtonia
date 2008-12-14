#ifndef ASTEROID_DRAWER_H
#define ASTEROID_DRAWER_H

#include "asteroid.h"

class AsteroidDrawer {
public:  
  static void draw(Object const *object);

private:
  static const int number_of_segments;
};

#endif 