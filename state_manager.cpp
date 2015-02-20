#include "state_manager.h"
#include "glgame.h"
#include "menu.h"

StateManager::StateManager() {
  state = new Menu();
}

StateManager::~StateManager() {
  delete state;
}

void StateManager::draw(int window) {
  state->draw(window);
}

void StateManager::mouse_move(int x, int y) {
  state->mouse_move(x, y);
}

void StateManager::keyboard(unsigned char key, int x, int y) {
  if(!key_states[key]) {
    key_states[key] = true;
    state->keyboard(key, x, y);
  }
}

void StateManager::keyboard_up(unsigned char key, int x, int y) {
  state->keyboard_up(key, x, y);
  key_states[key] = false;
}

void StateManager::update_lists() {
  state->update_lists();
}

void StateManager::controller(SDL_Event event) {
  if(event.type == SDL_CONTROLLERDEVICEADDED) {
    cout << "Controller added" << endl;
  } else if(event.type == SDL_CONTROLLERDEVICEREMOVED) {
    cout << "Controller removed" << endl;
  }
  state->controller(event);
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
  Typer::resize(x,y);
}
