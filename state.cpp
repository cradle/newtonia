#include "state.h"

State::State() : finished(false), next_state(NULL), ownership_transferred_(false) {}

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

bool State::ownership_transferred() {
  return ownership_transferred_;
}

void State::clear_state_change() {
  finished = false;
  next_state = NULL;
  ownership_transferred_ = false;
}

void State::request_state_change(State* next, bool transfer_ownership) {
  next_state = next;
  ownership_transferred_ = transfer_ownership;
  finished = true;
}

void State::mouse_move(int x, int y) {
  // std::cout << x << ", " << y << std::endl;
}