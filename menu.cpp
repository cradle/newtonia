#include "typer.h"
#include "highscore.h"
#include "glstarfield.h"
#include "glgame.h"
#include "menu.h"
#include "options.h"
#include "gl_compat.h"
#include "mat4.h"
#include <iostream>
#include <string>

static const float SENSITIVITY_VALUES[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
static const char* SENSITIVITY_LABELS[] = {"SLOW", "LOW", "NORMAL", "HIGH", "MAX"};
static const int NUM_SENSITIVITY = 5;

static int sensitivity_index_for(float value) {
  int best = 2;
  float best_dist = 1e6f;
  for (int i = 0; i < NUM_SENSITIVITY; i++) {
    float d = value > SENSITIVITY_VALUES[i] ? value - SENSITIVITY_VALUES[i]
                                             : SENSITIVITY_VALUES[i] - value;
    if (d < best_dist) { best_dist = d; best = i; }
  }
  return best;
}

const int Menu::default_world_width = 5000;
const int Menu::default_world_height = 5000;

Menu::Menu() :
  State(),
  currentTime(0),
  high_score(load_high_score()),
  has_save_(Save::save_exists()),
  menu_selection(0),
  viewpoint(Point(0,default_world_height/2)),
  starfield(GLStarfield(Point(default_world_width,default_world_height))) {
  g_options = load_options();
  sensitivity_index_ = sensitivity_index_for(g_options.keyboard_sensitivity);
#ifdef __EMSCRIPTEN__
  EM_ASM(if (window.setMenuMode) window.setMenuMode(1););
#endif
  if(music == NULL) {
    music = Mix_LoadMUS("audio/title.wav");
    if(music == NULL) {
      std::cout << "Unable to load title.wav (" << Mix_GetError() << ")" << std::endl;
    } else {
      Mix_PlayMusic(music, -1);
    }
  }
}

Menu::~Menu() {
  Mix_FreeMusic(music);
}

void Menu::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glViewport(0, 0, window.x(), window.y());

  // Perspective starfield: camera at origin looking down -Z, stars have negative z.
  float proj[16];
  mat4_perspective(proj, 90.0f, window.x() / window.y(), 100.0f, 2000.0f);

  float vp[16];
  mat4_translate(vp, proj, -viewpoint.x(), -viewpoint.y(), 0.0f);
  gles2_set_vp(vp);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);

  mat4_translate(vp, proj, -viewpoint.x() + default_world_width, -viewpoint.y(), 0.0f);
  gles2_set_vp(vp);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);

  mat4_translate(vp, proj, -viewpoint.x() - default_world_width, -viewpoint.y(), 0.0f);
  gles2_set_vp(vp);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);

  // Ortho overlay for text (identity model — Typer applies its own transforms via pre_draw).
  float ortho[16];
  mat4_ortho(ortho, -window.x(), window.x(), -window.y(), window.y(), -1.0f, 1.0f);
  gles2_set_vp(ortho);

  Typer::draw_centered(0, 200, "Newtonia", 80);
  if (high_score > 0 && !options_mode_) {
    Typer::draw_centered(0, -215, "HIGH SCORE", 14);
    Typer::draw_centered(0, -255, high_score, 18);
  }

  if (options_mode_) {
    Typer::draw_centered(0,  100, "OPTIONS", 30);
    Typer::draw_centered(0,   10, "KEYBOARD SENSITIVITY", 18);
    std::string sens = std::string("< ") + SENSITIVITY_LABELS[sensitivity_index_] + " >";
    Typer::draw_centered(0,  -70, sens.c_str(), 26);
    Typer::draw_centered(0, -200, "< > TO CHANGE   ENTER TO BACK", 14);
  } else if (has_save_) {
    if (is_touch_mode()) {
      // Side-by-side layout for touch: full left/right halves are tap targets
      Typer::draw_centered(-Typer::scaled_window_width / 2, -50, "CONTINUE", 26);
      Typer::draw_centered( Typer::scaled_window_width / 2, -50, "NEW GAME", 26);
    } else {
      // Stacked layout for keyboard/controller with selection indicator
      std::string cont    = std::string(menu_selection == 0 ? "> " : "  ") + "CONTINUE";
      std::string newgame = std::string(menu_selection == 1 ? "> " : "  ") + "NEW GAME";
      std::string options = std::string(menu_selection == 2 ? "> " : "  ") + "OPTIONS";
      Typer::draw_centered(0,  80, cont.c_str(),    22);
      Typer::draw_centered(0,  -10, newgame.c_str(), 22);
      Typer::draw_centered(0, -100, options.c_str(), 22);
    }
  } else {
    if (is_touch_mode()) {
      if ((currentTime/1400) % 2) {
        Typer::draw_centered(0, -50, "tap to start", 18);
      }
    } else {
      // Keyboard/controller: show NEW GAME and OPTIONS
      std::string newgame = std::string(menu_selection == 0 ? "> " : "  ") + "NEW GAME";
      std::string options = std::string(menu_selection == 1 ? "> " : "  ") + "OPTIONS";
      Typer::draw_centered(0,  -10, newgame.c_str(), 22);
      Typer::draw_centered(0, -100, options.c_str(), 22);
    }
  }
  Typer::draw_centered(0, -420, "© 2008-2026 METONYMOUS", 13, currentTime);
}

void Menu::tick(int delta) {
  currentTime += delta;
  viewpoint += Point(1,0) * (0.025 * delta);
  //FIX: Wrapping bug
  if(viewpoint.x() > default_world_width) {
      viewpoint += Point(-default_world_width,0);
  }

  // Poll R2 trigger directly each tick as a fallback for the first menu load,
  // where SDL may not have sent initial axis-motion events for the trigger yet.
  bool r2_pressed = false;
  int n = SDL_NumJoysticks();
  for(int i = 0; i < n; i++) {
    SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
    if(!ctrl) continue;
    if(SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) {
      r2_pressed = true;
      if(!r2_active) {
        r2_active = true;
        if (options_mode_) {
          close_options();
        } else if (menu_selection == max_menu_items() - 1) {
          open_options();
        } else {
          confirm_selection(ctrl);
        }
        return;
      }
      break;
    }
  }
  if(!r2_pressed) r2_active = false;
}

void Menu::controller(SDL_Event event) {
  if (options_mode_) {
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
        if (sensitivity_index_ > 0) sensitivity_index_--;
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
        if (sensitivity_index_ < NUM_SENSITIVITY - 1) sensitivity_index_++;
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A ||
                 event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                 event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
                 event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
        close_options();
      }
    } else if (event.type == SDL_CONTROLLERAXISMOTION) {
      if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
        bool l = event.caxis.value < -8000;
        bool r = event.caxis.value >  8000;
        if (l && !left_stick_left_active && sensitivity_index_ > 0) sensitivity_index_--;
        if (r && !left_stick_right_active && sensitivity_index_ < NUM_SENSITIVITY - 1) sensitivity_index_++;
        left_stick_left_active  = l;
        left_stick_right_active = r;
      }
    }
    return;
  }

  int n = max_menu_items();
  if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
      glutLeaveMainLoop();
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
      if (menu_selection > 0) menu_selection--;
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
      if (menu_selection < n - 1) menu_selection++;
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
      if (menu_selection == n - 1) {
        open_options();
      } else {
#ifdef __EMSCRIPTEN__
        EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
        confirm_selection(SDL_GameControllerFromInstanceID(event.cbutton.which));
      }
    }
  } else if (event.type == SDL_CONTROLLERAXISMOTION) {
    if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
      bool pressed = event.caxis.value > 8000;
      if (pressed && !r2_active) {
        r2_active = true;
        if (menu_selection == n - 1) {
          open_options();
        } else {
#ifdef __EMSCRIPTEN__
          EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
          confirm_selection(SDL_GameControllerFromInstanceID(event.caxis.which));
        }
      }
      if (!pressed) r2_active = false;
    } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
      bool up   = event.caxis.value < -8000;
      bool down = event.caxis.value >  8000;
      if (up   && !left_stick_up_active   && menu_selection > 0)     menu_selection--;
      if (down && !left_stick_down_active && menu_selection < n - 1) menu_selection++;
      left_stick_up_active   = up;
      left_stick_down_active = down;
    }
  }
}

void Menu::keyboard(unsigned char key, int x, int y) {
}

void Menu::keyboard_up(unsigned char key, int x, int y) {
  // Options screen: platform-agnostic
  if (options_mode_) {
    if (key == 'a' || key == 'A') {
      if (sensitivity_index_ > 0) sensitivity_index_--;
    } else if (key == 'd' || key == 'D') {
      if (sensitivity_index_ < NUM_SENSITIVITY - 1) sensitivity_index_++;
    } else if (key == 27 || key == ' ' || key == '\r' || key == '\n') {
      close_options();
    }
    return;
  }

  int n = max_menu_items();

#if defined(__ANDROID__) || defined(__IOS__)
  // Touch/mobile — touch_tap handles Continue/New Game when a save exists;
  // suppress \r so a finger-down on the left (joystick) half doesn't immediately
  // confirm before the user lifts their finger.
  if (has_save_ && (key == '\r' || key == '\n')) return;
  if (menu_selection == n - 1) {
    open_options();
  } else {
    confirm_selection(nullptr);
  }
#elif defined(__EMSCRIPTEN__)
  if (is_touch_mode()) {
    // Touch web: same as mobile — touch_tap handles selection, suppress \r
    if (has_save_ && (key == '\r' || key == '\n')) return;
    if (menu_selection == n - 1) {
      open_options();
    } else {
      EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
      confirm_selection(nullptr);
    }
  } else {
    // Keyboard web: w/s navigate, space/enter confirm
    if (key == ' ' || key == '\r' || key == '\n') {
      if (menu_selection == n - 1) {
        open_options();
      } else {
        EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
        confirm_selection(nullptr);
      }
    } else if (key == 'w' || key == 'W') {
      if (menu_selection > 0) menu_selection--;
    } else if (key == 's' || key == 'S') {
      if (menu_selection < n - 1) menu_selection++;
    }
  }
#else
  if (key == 27) {
    glutLeaveMainLoop();
  } else if (key == ' ' || key == '\r' || key == '\n') {
    if (menu_selection == n - 1) {
      open_options();
    } else {
      confirm_selection(nullptr);
    }
  } else if (key == 'w' || key == 'W') {
    if (menu_selection > 0) menu_selection--;
  } else if (key == 's' || key == 'S') {
    if (menu_selection < n - 1) menu_selection++;
  }
#endif
}

bool Menu::back_pressed() {
  return false; // signal the platform to quit the app
}

void Menu::touch_tap(float nx, float ny) {
  if (!has_save_) return;
  // Left half = CONTINUE, right half = NEW GAME
  menu_selection = (nx >= 0.5f) ? 1 : 0;
#ifdef __EMSCRIPTEN__
  EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
  confirm_selection(nullptr);
}

int Menu::max_menu_items() const {
  return has_save_ ? 3 : 2;
}

void Menu::open_options() {
  options_mode_ = true;
}

void Menu::close_options() {
  g_options.keyboard_sensitivity = SENSITIVITY_VALUES[sensitivity_index_];
  save_options(g_options);
  options_mode_ = false;
}

void Menu::confirm_selection(SDL_GameController *ctrl) {
  if (has_save_ && menu_selection == 0) {
    Save::GameState s;
    if (Save::load_game(s)) {
      request_state_change(new GLGame(s, ctrl));
      return;
    }
    // Corrupt or missing save — fall through to new game
    has_save_ = false;
  }
  request_state_change(new GLGame(ctrl));
}
