#ifndef WRAPPED_POINT_H
#define WRAPPED_POINT_H
#include "point.h"

class WrappedPoint : public Point {
public:
  WrappedPoint() {};
  WrappedPoint(float x, float y) : Point(x,y) {};
  WrappedPoint(Point other) : Point(other) {};
  
  void wrap(); 
  
  static void set_boundaries(Point bounds);
  
  static bool cross_boundary(Point start, Point end);
  
private:
  static float x_min;
  static float x_max, y_min, y_max;
};
#endif