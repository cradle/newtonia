#ifndef MENU_H
#define MENU_H

#include <SDL.h>
#include <vector>
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
  // The single menu decision ladder: w/s move, a/d adjust (options),
  // Enter/space confirm, Esc backs out one level. Fed by keyboard_up (after
  // per-platform touch filtering) and by controller() via the shared
  // State::nav_key_from_controller translation, so keyboard and pad behave
  // identically on every menu screen. src is the pad that produced a
  // confirm (bound to player 1), null for real keyboard input.
  void nav_input(unsigned char key, SDL_GameController *src);
  void confirm_selection(SDL_GameController *ctrl);
  int  max_menu_items() const;
  // ONLINE row (netplay lobby): only on builds with a net backend.
  bool show_online_row() const;
  int  online_row_index() const;  // -1 when the row is hidden
  // OPTIONS row: always shown. The options screen works on keyboard,
  // controller, and touch (tap a row to cycle its value).
  bool show_options_row() const;
  // Shared vertical row layout for the main menu (desktop cursor rows and
  // touch tap targets use the same geometry).
  static int menu_row_size();
  void draw_menu_rows(const std::vector<std::string> &rows);
  int  menu_row_at(float ny) const;  // -1 when the tap misses every row
  void open_options();
  void close_options();
  void adjust_active_row(int delta, bool wrap = false);

  int currentTime;
  int high_score;
  bool has_save_ = false;
  int  menu_selection = 0;
  bool options_mode_ = false;
  int  sensitivity_index_[2] = {2, 2};  // per-player index into SENSITIVITY_VALUES
  int  smoothing_index_[2]   = {1, 1};  // per-player index into SMOOTHING_VALUES (1=NORMAL=0.004)
  int  star_density_index_   = 4;       // index into STAR_DENSITY_MULTIPLIERS (4=full)
  int  camera_index_[2]      = {1, 1};  // per-player: 0=FIXED, 1=ROTATE
  int  active_row_ = 0;                 // index into the options row list (see opt_row)
  WrappedPoint viewpoint;
  GLStarfield *starfield;
  static const int default_world_width, default_world_height;

  Mix_Music *music = NULL;

  bool quit_confirm_ = false;
  int  quit_selection_ = 0;  // 0 = Yes, 1 = No
  bool new_confirm_ = false; // confirm NEW GAME when it would overwrite a save
  int  new_selection_ = 1;   // 0 = Yes, 1 = No — No is the safe default
  bool attract_mode_ = true; // flashing PRESS ENTER/START (TAP TO START on touch) before the menu
};

#endif
