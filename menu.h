#ifndef MENU_H
#define MENU_H

#include "state.h"

class Menu : public State {
public:
  Menu();
  
  void draw();
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void tick(int delta);
  
private:
  Typer typer;
  WrappedPoint viewpoint;
  GLStarfield starfield;
};

#endif