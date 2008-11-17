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
  void set_keys(int left, int right, int up, int right);
  virtual void draw();
  
  static void collide(GLShip& first, GLShip& second);
  
protected:
  Ship ship;
  float window_width, window_height;
  
  int thrust_key;
  int left_key;
  int right_key;
  int shoot_key;
};

#endif
