#include "wrapped_point.h"
#include "point.h"

#include <iostream>

Point WrappedPoint::max = Point();

WrappedPoint::WrappedPoint() {
  coords[X] = rand()%int(max.x());
  coords[Y] = rand()%int(max.y());
}

void WrappedPoint::wrap() {
  this %= max;
}

void WrappedPoint::wrap_to(int x, int y) {
  this %= Point(x, y);
}

Point WrappedPoint::closest_to(const Point other) const {
  Point closest = *this, current;
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      current = *this + (Point(x,y) * max);
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
  max = bounds;
}
