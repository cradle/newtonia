#include "wrapped_point.h"
#include "point.h"

#include <iostream>

float WrappedPoint::x_min = 0;
float WrappedPoint::y_min = 0;
float WrappedPoint::x_max = 0;
float WrappedPoint::y_max = 0;

void WrappedPoint::wrap() {
  float width = x_max - x_min, height = y_max - y_min;
  while(coords[X] < x_min)
    coords[X] += width;
  while(coords[X] > x_max)
    coords[X] -= width;
  while(coords[Y] < y_min)
    coords[Y] += height;
  while(coords[Y] > y_max)
    coords[Y] -= height;
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
  x_min = -bounds.x();
  x_max = bounds.x();
  y_min = -bounds.y();
  y_max = bounds.y();
}
