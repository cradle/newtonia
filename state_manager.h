#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include "state.h"
#include <map>
#include <SDL.h>

class StateManager {
public:
  StateManager();
  ~StateManager();

  void draw(int window);
  void mouse_move(int x, int y);
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void controller(SDL_Event event);
  void tick(int delta);
  void resize(int x, int y);

private:
  Point window;
  State *state;

  map<const char, bool> key_states;
};

#endif
