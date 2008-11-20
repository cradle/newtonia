#ifndef GL_TRAIL_H
#define GL_TRAIL_H

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif


#include "ship.h"
#include <deque>

class GLTrail {
public:
  enum TYPE { 
    DOTS = GL_POINTS, 
    LINE = GL_LINE_STRIP
  };
  
  GLTrail(Ship* ship, TYPE type, float deviation = 0.05, float offset = 0);
  void draw();
  void step(float delta);
  
private:
  void add();
  
  float deviation;
  float offset;
  TYPE type;
  Ship* ship;
  
  std::deque<Bullet*> trail;
};

#endif