#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include "state.h"
#include <map>
#include <SDL.h>

class StateManager {
public:
  StateManager(bool screenshot_mode = false);
  ~StateManager();

  void draw();
  void mouse_move(int x, int y);
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void controller(SDL_Event event);
  void tick(int delta);
  void resize(int x, int y);
  void touch_joystick(float nx, float ny);
  void focus_lost();
  void focus_gained();
  void controller_added(SDL_GameController *ctrl);
  void controller_removed(SDL_JoystickID id);

private:
  SDL_GameController *active_controllers[2] = {NULL, NULL};
  SDL_JoystickID active_controller_ids[2] = {-1, -1};
  Point window;
  State *state;

  map<const char, bool> key_states;
};

#endif
