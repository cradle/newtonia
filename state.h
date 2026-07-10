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
  Point window;

private:
  bool finished;
  State* next_state;
  bool ownership_transferred_;
};

#endif
