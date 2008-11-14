#ifndef WRAPPED_POINT_H
#define WRAPPED_POINT_H
#include "point.h"

class WrappedPoint : public Point {
public:
  WrappedPoint() {};
  WrappedPoint(float x, float y) : Point(x,y) {};
  WrappedPoint(Point other) : Point(other) {};
  void set_boundaries(float left, float top, float right, float bottom);
  //TODO: learn inheritance on operators
  void wrap(); 

private:  
  //TODO: make boundaries a class/struct, rename to WrappedPoint
  int x_min;
  int x_max;
  int y_min;
  int y_max;
};

#endif