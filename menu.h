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
  bool back_pressed() override;

private:
  void confirm_selection(SDL_GameController *ctrl);
  int  max_menu_items() const;
  void open_options();
  void close_options();
  void adjust_active_row(int delta);

  int currentTime;
  int high_score;
  bool has_save_ = false;
  int  menu_selection = 0;
  bool options_mode_ = false;
  int  sensitivity_index_[2] = {2, 2};  // per-player index into SENSITIVITY_VALUES
  int  smoothing_index_[2]   = {1, 1};  // per-player index into SMOOTHING_VALUES (1=NORMAL=0.004)
  int  active_row_ = 0;                 // 0=P1 sens, 1=P1 smooth, 2=P2 sens, 3=P2 smooth
  WrappedPoint viewpoint;
  GLStarfield starfield;
  static const int default_world_width, default_world_height;

  Mix_Music *music = NULL;
  bool r2_active = false;
  bool left_stick_up_active = false;
  bool left_stick_down_active = false;
  bool left_stick_left_active = false;
  bool left_stick_right_active = false;

  bool quit_confirm_ = false;
  int  quit_selection_ = 0;  // 0 = Yes, 1 = No
  bool attract_mode_ = true; // show flashing PRESS ENTER/START before menu on desktop
};

#endif
