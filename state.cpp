#include "state.h"

State::State() : finished(false) {}

void State::resize(int x, int y) {
  window = Point(x, y);
  glClearAccum(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_ACCUM_BUFFER_BIT);
}

bool State::is_finished() {
  return finished;
}

State* State::get_next_state() {
  return next_state;
}

void State::request_state_change(State* next) {
  next_state = next;
  finished = true;
}

void State::mouse_move(int x, int y) {
  // std::cout << x << ", " << y << std::endl;
}