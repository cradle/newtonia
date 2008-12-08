#ifndef STATE_H
#define STATE_H

#include "point.h"
#include "wrapped_point.h"
#include "typer.h"
#include "glstarfield.h"

class State {
public:
  State();  
  
  virtual void draw();
  virtual void keyboard(unsigned char key, int x, int y);
  virtual void keyboard_up(unsigned char key, int x, int y);
  virtual void resize(int width, int height);
  virtual void tick(int delta);
  
protected:
  int last_time;
  Point window;
  Typer typer;
  WrappedPoint viewpoint;
  GLStarfield starfield;
};

#endif