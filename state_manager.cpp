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
  if(key == '2') {
    delete state;
    state = new GLGame(10000, 10000, 2);
    state->resize(window.x(), window.y());
  } else if(key == '1') {
    delete state;
    state = new GLGame(10000, 10000, 1);
    state->resize(window.x(), window.y());
  } else if(key == 27) { // escape
    delete state;
    state = new Menu();
    state->resize(window.x(), window.y());
  }
  state->keyboard_up(key, x, y);
}

void StateManager::tick(int delta) {
  state->tick(delta);
}

void StateManager::resize(int x, int y) {
  window = Point(x,y);
  state->resize(x,y);
}