#ifndef WRAPPED_POINT_H
#define WRAPPED_POINT_H
#include "point.h"

class WrappedPoint : public Point {
public:
  WrappedPoint() {};
  WrappedPoint(float x, float y) : Point(x,y) {};
  WrappedPoint(Point other) : Point(other) {};
  
  void wrap(); 
  
  void set_boundaries(Point bounds);
  
private:  
  int x_min, x_max, y_min, y_max;
};

#endif