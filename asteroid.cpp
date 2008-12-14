#include "asteroid.h"
#include "wrapped_point.h"

#include <iostream>

const int Asteroid::max_speed = 5;

Asteroid::Asteroid() {
  position = WrappedPoint();//rand(), rand());
  radius = rand()%275 + 25.0f;
  radius_squared = radius*radius;
  velocity = Point(rand()-RAND_MAX/2, rand()-RAND_MAX/2).normalized()*max_speed/radius;
}