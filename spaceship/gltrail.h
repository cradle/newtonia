#ifndef GL_TRAIL_H
#define GL_TRAIL_H

#include "ship.h"
#include <deque>

class GLTrail {
public:
  GLTrail(Ship* ship);
  void start();
  void draw();
  void step(float delta);
  
private:
  void add();
  
  Ship* ship;
  
  std::deque<Bullet*> trail;
};

#endif