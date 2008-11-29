#include "wrapped_point.h"
#include "point.h"

#include <iostream>

float WrappedPoint::x_min = 0;
float WrappedPoint::y_min = 0;
float WrappedPoint::x_max = 0;
float WrappedPoint::y_max = 0;

void WrappedPoint::wrap() {
  while(x() < x_min)
    coords[X] += x_max - x_min;
  while(x() > x_max)
    coords[X] -= x_max - x_min;
  while(y() < y_min)
    coords[Y] += y_max - y_min;
  while(y() > y_max)
    coords[Y] -= y_max - y_min;
}

Point WrappedPoint::closest_to(Point other) {
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

void WrappedPoint::set_boundaries(Point bounds) {
  x_min = -bounds.x();
  x_max = bounds.x();
  y_min = -bounds.y();
  y_max = bounds.y();
}
