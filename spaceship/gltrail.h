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
  
  GLTrail(Ship* ship, float deviation = 0.05, float offset = 0, float speed = 0.25);
  void split();
  void draw();
  void step(float delta);
  
private:
  void add();
  
  float deviation;
  float offset;
  float speed;
  Ship* ship;
  
  std::deque<Bullet*> trail;
};

#endif