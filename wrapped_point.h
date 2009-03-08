#ifndef WRAPPED_POINT_H
#define WRAPPED_POINT_H
#include "point.h"

class WrappedPoint : public Point {
public:
  WrappedPoint();
  WrappedPoint(float x, float y) : Point(x,y) {};
  WrappedPoint(const Point other) : Point(other) {};

  float distance_to(const WrappedPoint other) const;
  Point closest_to(const Point other) const;
  void wrap(float x_max = WrappedPoint::x_max, float y_max = WrappedPoint::y_max);

  static void set_boundaries(const Point bounds);

private:
  static float x_min, x_max, y_min, y_max;
};
#endif
