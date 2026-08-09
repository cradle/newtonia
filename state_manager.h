#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include "state.h"
#include "preferences.h" // MAX_PLAYERS sizes the pad registry
#include <map>
#include <SDL.h>

class StateManager {
public:
  StateManager();
  ~StateManager();

  void draw();
  void mouse_move(int x, int y);
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void controller(SDL_Event event);
  void tick(int delta);
  void resize(int x, int y);
  void touch_joystick(float nx, float ny);
  void touch_tap(float nx, float ny);
  // DEBUG (NEWTONIA_BETA only): a tap in the very top-right corner skips
  // the level. One implementation for every touch platform's finger_down —
  // returns true when the tap was consumed. Call BEFORE any pause zones
  // (the corner sits inside them) — see the call sites in web_main.cpp /
  // android_main.cpp.
  bool debug_skip_corner_tap(float nx, float ny);
  // True while an online session (in-game or lobby) needs the loop to
  // keep ticking even when the browser tab is hidden (web build).
  bool wants_background_ticks();
  // True while the current screen is consuming typed text (the lobby's
  // room-code entry): global single-key hotkeys — the bare F fullscreen
  // toggle — must not fire on keystrokes meant for the text field (the
  // Deck's floating keyboard typing F flickered fullscreen; Glenn).
  bool text_entry_active();
  bool back_pressed();
  void focus_lost();
  void focus_gained();
  void controller_added(SDL_GameController *ctrl);
  void controller_removed(SDL_JoystickID id);

private:
  SDL_GameController *active_controllers[MAX_PLAYERS] = {};
  SDL_JoystickID active_controller_ids[MAX_PLAYERS] = {-1, -1, -1, -1}; // one -1 per slot: 0 is a VALID SDL instance id
  Point window;
  State *state;

  map<const char, bool> key_states;
  bool focus_muted = false;
};

#endif
