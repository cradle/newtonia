#ifndef GLSTARFIELD_H
#define GLSTARFIELD_H

#include "point.h"

class GLStarfield {
public:
  GLStarfield(Point size);
  
  void draw(Point velocity, Point viewpoint);
  
private:
  static const int NUM_LAYERS = 10;
  static const int NUM_STARS = 100;
  Point* stars[NUM_LAYERS][NUM_STARS];  
};

#endif