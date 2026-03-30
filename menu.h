#ifndef MENU_H
#define MENU_H

#include <SDL.h>
#include "state.h"
#include "savegame.h"

class Menu : public State {
public:
  Menu();
  virtual ~Menu();

  void draw() override;
  void keyboard(unsigned char key, int x, int y) override;
  void keyboard_up(unsigned char key, int x, int y) override;
  void controller(SDL_Event event) override;
  void tick(int delta) override;
  void touch_tap(float nx, float ny) override;

private:
  void confirm_selection(SDL_GameController *ctrl);

  int currentTime;
  int high_score;
  bool has_save_ = false;
  int  menu_selection = 0;  // 0 = CONTINUE, 1 = NEW GAME (only relevant when has_save_)
  WrappedPoint viewpoint;
  GLStarfield starfield;
  static const int default_world_width, default_world_height;

  Mix_Music *music = NULL;
  bool r2_active = false;
};

#endif
