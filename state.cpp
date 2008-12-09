#include "state.h"

State::State() : finished(false) {}

void State::resize(int x, int y) {
  window = Point(x, y);
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