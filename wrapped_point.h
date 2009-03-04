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
  void wrap();
  void wrap_to(int x, int y);

  static void set_boundaries(const Point bounds);

private:
  static Point max;
};
#endif
