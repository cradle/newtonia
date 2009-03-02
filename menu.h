#ifndef MENU_H
#define MENU_H

#include "state.h"

class Menu : public State {
public:
  Menu();
  virtual ~Menu() {};
  
  void draw();
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void tick(int delta);
  
private:
  Typer typer;
  int currentTime;
  WrappedPoint viewpoint;
  GLStarfield starfield;
  
  enum MenuItem {
    ARCADE,
    VERSUS    
  };
  
  MenuItem selected;
};

#endif