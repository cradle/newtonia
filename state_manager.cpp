#include "state_manager.h"
#include "glgame.h"
#include "menu.h"

StateManager::StateManager() {
  state = new Menu();
}

StateManager::~StateManager() {
  delete state;
}

void StateManager::draw() {
  state->draw();
}

void StateManager::keyboard(unsigned char key, int x, int y) {
  state->keyboard(key, x, y);
}

void StateManager::keyboard_up(unsigned char key, int x, int y) {
  state->keyboard_up(key, x, y);
}

void StateManager::tick(int delta) {
  if(state->is_finished()) {
    State* next_state = state->get_next_state();
    next_state->resize(window.x(), window.y());
    delete state;
    state = next_state;
  }
  state->tick(delta);
}

void StateManager::resize(int x, int y) {
  window = Point(x,y);
  state->resize(x,y);
}