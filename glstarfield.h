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
  virtual ~GLStarfield();
  
  void draw_rear(Point const viewpoint) const;
  void draw_front(Point const viewpoint) const;
  
private:
  GLuint point_layers;
  static const int NUM_REAR_LAYERS;
  static const int NUM_FRONT_LAYERS;
  static const float STAR_DENSITY;
};

#endif
