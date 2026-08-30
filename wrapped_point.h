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

  static void set_boundaries(const Point bounds);

  // World width. The boundaries are process-global, so drawers can size
  // world-proportional geometry (the minimap dots, which must hold their
  // on-screen size as the world grows) without plumbing the world through.
  static float x_span() { return x_max - x_min; }

private:
  static float x_min, x_max, y_min, y_max;
};
#endif
