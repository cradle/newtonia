#ifndef MENU_H
#define MENU_H

#include <SDL.h>
#include "state.h"

class Menu : public State {
public:
  Menu();
  virtual ~Menu();

  void draw();
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void controller(SDL_Event event);
  void tick(int delta);

private:
  int currentTime;
  int high_score;
  WrappedPoint viewpoint;
  GLStarfield starfield;
  static const int default_world_width, default_world_height;

  Mix_Music *music = NULL;
};

#endif
