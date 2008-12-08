#ifndef GLSTARFIELD_H
#define GLSTARFIELD_H

#include "point.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

class GLStarfield {
public:
  GLStarfield(Point const size);
  
  void draw_rear(Point const viewpoint) const;
  void draw_front(Point const viewpoint) const;
  
private:
  GLuint point_layers;
  static const int NUM_REAR_LAYERS = 20;
  static const int NUM_FRONT_LAYERS = 5;
  static const int NUM_STARS = 1600;
};

#endif
