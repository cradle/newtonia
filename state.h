#ifndef STATE_H
#define STATE_H

#include "point.h"
#include "wrapped_point.h"
#include "typer.h"
#include "glstarfield.h"
#include <SDL.h>
#include "preferences.h"  // MAX_PLAYERS sizes the nav-pad latches

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
  // Stick nav thresholds (shared by every screen): arm past ON, release
  // inside OFF — the gap stops a stick hovering near the threshold from
  // repeating (the Mac-gamepad sensitivity fix, now in one place).
  static const int NAV_STICK_ON  = 16000;
  static const int NAV_STICK_OFF = 8000;
  Point window;

  // A state can be entered MID-HOLD: the auto-rejoin lobby arrives from
  // gameplay with the fire trigger plausibly still down, and this fresh
  // state's right-trigger edge starts "released" — so the hold's next
  // axis sample (jitter, or the release ramp) would read as a fresh
  // confirm. Pre-arm the edge: the trigger must be SEEN released before
  // it can confirm in this state.
  void nav_assume_rt_held() {
    // The caller doesn't know which pad is mid-hold, so pre-arm them all —
    // exactly what the old shared latch did.
    for (int i = 0; i < NAV_PADS; i++) nav_pads_[i].rt = true;
  }

private:
  // nav_key_from_controller hysteresis, PER PAD: one shared latch let a
  // drifting idle pad hold the arm and deaden stick-nav for everyone
  // (FOURPLAYER.md A4). Slots claim by instance id, first-free; more pads
  // than slots degrades to sharing slot 0 (never expected — opens are
  // bounded by the seat cap).
  static const int NAV_PADS = MAX_PLAYERS;
  struct NavPad {
    SDL_JoystickID id = -1;
    bool stick[4] = {false, false, false, false};  // up/down/left/right armed
    bool rt = false;                               // right-trigger edge
  };
  NavPad nav_pads_[NAV_PADS];
  NavPad &nav_pad(SDL_JoystickID which);

  bool finished;
  State* next_state;
  bool ownership_transferred_;
};

#endif
