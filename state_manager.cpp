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

void StateManager::controller(SDL_Event event) {
  state->controller(event);
}

void StateManager::controller_added(SDL_GameController *ctrl) {
  SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctrl));
  for(int i = 0; i < 2; i++) {
    if(active_controllers[i] == NULL) {
      active_controllers[i] = ctrl;
      active_controller_ids[i] = id;
      break;
    }
  }
  GLGame *game = dynamic_cast<GLGame*>(state);
  if(game) game->controller_added(ctrl);
}

void StateManager::controller_removed(SDL_JoystickID id) {
  for(int i = 0; i < 2; i++) {
    if(active_controller_ids[i] == id) {
      active_controllers[i] = NULL;
      active_controller_ids[i] = -1;
      break;
    }
  }
  GLGame *game = dynamic_cast<GLGame*>(state);
  if(game) game->controller_removed(id);
}

void StateManager::tick(int delta) {
  if(state->is_finished()) {
    State* next_state = state->get_next_state();
    next_state->resize(window.x(), window.y());
    delete state;
    state = next_state;
    GLGame *game = dynamic_cast<GLGame*>(state);
    if(game) {
      for(int i = 0; i < 2; i++) {
        if(active_controllers[i]) game->controller_added(active_controllers[i]);
      }
    }
  }
  state->tick(delta);
}

void StateManager::resize(int x, int y) {
  window = Point(x,y);
  state->resize(x,y);
  Typer::resize(x,y);
}

void StateManager::touch_joystick(float nx, float ny) {
  GLGame *game = dynamic_cast<GLGame*>(state);
  if(game) game->touch_joystick(nx, ny);
}

void StateManager::focus_lost() {
  GLGame *game = dynamic_cast<GLGame*>(state);
  if(game) game->focus_lost();
}

void StateManager::focus_gained() {
  GLGame *game = dynamic_cast<GLGame*>(state);
  if(game) game->focus_gained();
}
