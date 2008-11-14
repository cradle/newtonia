#include "wrapped_point.h"
#include "point.h"

void WrappedPoint::wrap() {
  while(x < x_min)
    x += x_max - x_min;
  while(x > x_max)
    x -= x_max - x_min;
  while(y < y_min)
    y += y_max - y_min;
  while(y > y_max)
    y -= y_max - y_min;
}

void WrappedPoint::set_boundaries(float left, float top, float right, float bottom) {
  x_min = left;
  x_max = right;
  y_min = bottom;
  y_max = top;
  wrap();
}