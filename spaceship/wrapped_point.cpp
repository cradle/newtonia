#include "wrapped_point.h"
#include "point.h"

#include <iostream>

float WrappedPoint::x_min = 0;
float WrappedPoint::y_min = 0;
float WrappedPoint::x_max = 0;
float WrappedPoint::y_max = 0;

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

void WrappedPoint::set_boundaries(Point bounds) {
  x_min = -bounds.x;
  x_max = bounds.x;
  y_min = -bounds.y;
  y_max = bounds.y;
}