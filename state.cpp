#include "state.h"

State::State() : finished(false), next_state(NULL), ownership_transferred_(false) {}

void State::resize(int x, int y) {
  window = Point(x, y);
  // No accumulation-buffer clear here. It dated from the fixed-function
  // build and cannot work on anything this game now runs on: accumulation
  // buffers were removed in GL 3.2 core (the desktop context) and never
  // existed in GLES2, where gles2_compat.h stubs glClearAccum to nothing but
  // still hands glClear the legacy 0x200 bit — an invalid mask, which WebGL
  // reports as "INVALID_VALUE: glClear: Invalid mask bits" (field, private
  // Safari, 2026-07-29) and desktop core GL swallows silently. Every resize
  // raised it: load, rotate, URL-bar show/hide. Nothing accumulates and
  // nothing drew it, so the call only ever produced the error.
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
                                             PadId *src) {
  unsigned char key = 0;
  PadId which = PAD_NONE;
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
    NavPad &pad = nav_pad(which);
    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
      bool up   = v < -(pad.stick[0] ? NAV_STICK_OFF : NAV_STICK_ON);
      bool down = v >  (pad.stick[1] ? NAV_STICK_OFF : NAV_STICK_ON);
      if (up   && !pad.stick[0]) key = 'w';
      if (down && !pad.stick[1]) key = 's';
      pad.stick[0] = up;
      pad.stick[1] = down;
    } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
      bool l = v < -(pad.stick[2] ? NAV_STICK_OFF : NAV_STICK_ON);
      bool r = v >  (pad.stick[3] ? NAV_STICK_OFF : NAV_STICK_ON);
      if (l && !pad.stick[2]) key = 'a';
      if (r && !pad.stick[3]) key = 'd';
      pad.stick[2] = l;
      pad.stick[3] = r;
    } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
      // Triggers spring fully back to zero, so a plain edge suffices.
      bool pressed = v > 8000;
      if (pressed && !pad.rt) key = '\r';
      pad.rt = pressed;
    }
  }
  if (key && src)
    *src = which;
  return key;
}
State::NavPad &State::nav_pad(PadId which) {
  for (int i = 0; i < NAV_PADS; i++)
    if (nav_pads_[i].id == which) return nav_pads_[i];
  for (int i = 0; i < NAV_PADS; i++)
    if (nav_pads_[i].id == PAD_NONE) { nav_pads_[i].id = which; return nav_pads_[i]; }
  return nav_pads_[0];
}
