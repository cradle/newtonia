#ifndef STATE_H
#define STATE_H

#include "point.h"
#include "wrapped_point.h"
#include "typer.h"
#include "glstarfield.h"

class State {
public:
  virtual void draw() = 0;
  virtual void keyboard(unsigned char key, int x, int y) = 0;
  virtual void keyboard_up(unsigned char key, int x, int y) = 0;
  virtual void tick(int delta) = 0;
  
  virtual void resize(int width, int height);
  
protected:
  Point window;
};

#endif