#ifndef GL_TRAIL_H
#define GL_TRAIL_H

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif


#include "ship.h"
#include <deque>

class GLTrail {
public: 
  enum TYPE {
    THRUSTING = 1,
    REVERSING = 2,
    LEFT = 4,
    RIGHT = 8
  };
  
  GLTrail(Ship* ship, 
          float deviation = 0.05, 
          Point offset = Point(), 
          float speed = 0.25, 
          float rotation = 0.0,
          int type = THRUSTING);
  void split();
  void draw();
  void step(float delta);
  
private:
  void add();
  
  int type;
  float deviation;
  float rotation;
  Point offset;
  float speed;
  Ship* ship;
  
  std::deque<Particle*> trail;
};

#endif
