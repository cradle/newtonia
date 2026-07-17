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

unsigned char State::nav_key_from_controller(const SDL_Event &e,
                                             SDL_GameController **src) {
  unsigned char key = 0;
  SDL_JoystickID which = -1;
  if (e.type == SDL_CONTROLLERBUTTONDOWN) {
    which = e.cbutton.which;
    switch (e.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_DPAD_UP:    key = 'w';  break;
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  key = 's';  break;
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  key = 'a';  break;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = 'd';  break;
      case SDL_CONTROLLER_BUTTON_A:
      case SDL_CONTROLLER_BUTTON_START:      key = '\r'; break;
      case SDL_CONTROLLER_BUTTON_B:
      case SDL_CONTROLLER_BUTTON_BACK:       key = 27;   break;
      default: break;
    }
  } else if (e.type == SDL_CONTROLLERAXISMOTION) {
    which = e.caxis.which;
    int v = e.caxis.value;
    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
      bool up   = v < -(nav_stick_[0] ? NAV_STICK_OFF : NAV_STICK_ON);
      bool down = v >  (nav_stick_[1] ? NAV_STICK_OFF : NAV_STICK_ON);
      if (up   && !nav_stick_[0]) key = 'w';
      if (down && !nav_stick_[1]) key = 's';
      nav_stick_[0] = up;
      nav_stick_[1] = down;
    } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
      bool l = v < -(nav_stick_[2] ? NAV_STICK_OFF : NAV_STICK_ON);
      bool r = v >  (nav_stick_[3] ? NAV_STICK_OFF : NAV_STICK_ON);
      if (l && !nav_stick_[2]) key = 'a';
      if (r && !nav_stick_[3]) key = 'd';
      nav_stick_[2] = l;
      nav_stick_[3] = r;
    } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
      // Triggers spring fully back to zero, so a plain edge suffices.
      bool pressed = v > 8000;
      if (pressed && !nav_rt_) key = '\r';
      nav_rt_ = pressed;
    }
  }
  if (key && src)
    *src = SDL_GameControllerFromInstanceID(which);
  return key;
}