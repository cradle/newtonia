#ifndef STATE_H
#define STATE_H

#include "point.h"
#include "wrapped_point.h"
#include "typer.h"
#include "glstarfield.h"
#include <SDL.h>

class State {
public:
  State();
  virtual ~State() {};

  virtual void draw() = 0;
  virtual void mouse_move(int x, int y);
  virtual void keyboard(unsigned char key, int x, int y) = 0;
  virtual void keyboard_up(unsigned char key, int x, int y) = 0;
  virtual void controller(SDL_Event event) = 0;
  virtual void tick(int delta) = 0;
  bool is_finished();
  State* get_next_state();
  // True when this state's ownership passed to the next state — the
  // StateManager must not delete it on transition (see Intro, which adopts
  // the GLGame it interrupts and later hands it back).
  bool ownership_transferred();
  // Reset a completed transition so a previously-run state can be
  // reinstalled as the current state (a game handed back by an intro still
  // carries the finished flag from when it requested the intro).
  void clear_state_change();

  virtual void touch_tap(float nx, float ny) {}
  virtual bool back_pressed() { return false; } // true = handled, false = quit app
  virtual void resize(int width, int height);

protected:
  // transfer_ownership: the next state takes ownership of this one instead
  // of the StateManager deleting it on transition.
  void request_state_change(State* next_state, bool transfer_ownership = false);
  // Arrow keys alias WASD in every menu, on every platform. Desktop GLUT
  // delivers specials as 128 + GLUT_KEY_* (LEFT=100, UP=101, RIGHT=102,
  // DOWN=103 — the same scheme preferences.h uses for F-key bindings), and
  // the SDL entry points translate arrow keysyms to the same codes. Menus
  // call this on the incoming key so the rest of their handler only ever
  // sees the wasd form.
  static unsigned char nav_key(unsigned char key) {
    switch (key) {
      case 128 + 101: return 'w';  // GLUT_KEY_UP
      case 128 + 103: return 's';  // GLUT_KEY_DOWN
      case 128 + 100: return 'a';  // GLUT_KEY_LEFT
      case 128 + 102: return 'd';  // GLUT_KEY_RIGHT
      default: return key;
    }
  }
  // Controller twin of nav_key: translate a pad event into the logical menu
  // key the state's keyboard ladder already understands — dpad and left
  // stick become w/a/s/d, A/Start/right-trigger become Enter (confirm),
  // B/Back become Esc (back out one level). Returns 0 for anything a menu
  // doesn't navigate by. Stick directions arm at ±NAV_STICK_ON and release
  // at ±NAV_STICK_OFF (hysteresis state lives per-State here, so every
  // screen gets the same feel without hand-rolled flag sets). When a key is
  // produced and src is non-null, *src receives the pad that pressed it
  // (menus bind that pad to player 1 on confirm). States with richer pad
  // semantics (gameplay, the lobby's code picker) handle those events
  // BEFORE falling back to this.
  unsigned char nav_key_from_controller(const SDL_Event &e,
                                        SDL_GameController **src = NULL);
  Point window;

private:
  // nav_key_from_controller hysteresis: stick up/down/left/right armed, and
  // the right-trigger edge.
  bool nav_stick_[4] = {false, false, false, false};
  bool nav_rt_ = false;

  bool finished;
  State* next_state;
  bool ownership_transferred_;
};

#endif
