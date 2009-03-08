#include "wrapped_point.h"
#include "point.h"

#include <iostream>

float WrappedPoint::x_min = 0;
float WrappedPoint::y_min = 0;
float WrappedPoint::x_max = 0;
float WrappedPoint::y_max = 0;

WrappedPoint::WrappedPoint() {
  coords[X] = rand()%int(x_max - x_min) + x_min;
  coords[Y] = rand()%int(y_max - y_min) + y_min;
}

void WrappedPoint::wrap(float x_max, float y_max) {
  while(coords[X] < 0)
    coords[X] += x_max;
  while(coords[X] > x_max)
    coords[X] -= x_max;
  while(coords[Y] < 0)
    coords[Y] += y_max;
  while(coords[Y] > y_max)
    coords[Y] -= y_max;
}

Point WrappedPoint::closest_to(const Point other) const {
  Point closest = *this, current;
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      current = *this + Point((x_max-x_min)*x, (y_max-y_min)*y);
      if((other - current).magnitude_squared() < (other - closest).magnitude_squared()) {
        closest = current;
      }
    }
  }
  return closest;
}

float WrappedPoint::distance_to(const WrappedPoint other) const {
  return (*this - other.closest_to(*this)).magnitude();
}

void WrappedPoint::set_boundaries(const Point bounds) {
  x_min = 0;
  x_max = bounds.x();
  y_min = 0;
  y_max = bounds.y();
}
