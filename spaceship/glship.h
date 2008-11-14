#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"

class GLShip {
public:
  GLShip() {};
  GLShip(int x, int y);
  void step(float delta);
  void resize(float screen_width, float screen_height);
  void input(unsigned char key, bool pressed = true);
  void draw();
  
private:
  Ship ship;
  float window_width, window_height;
};

#endif