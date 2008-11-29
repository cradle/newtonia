#ifndef GLSTARFIELD_H
#define GLSTARFIELD_H

#include "point.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

class GLStarfield {
public:
  GLStarfield(Point size);
  
  void draw(Point velocity, Point viewpoint);
  
private:
  GLuint point_layers;
  static const int NUM_REAR_LAYERS = 20;
  static const int NUM_FRONT_LAYERS = 5;
  static const int NUM_STARS = 3000;
};

#endif