#include "typer.h"
#include "highscore.h"
#include "glstarfield.h"
#include "glgame.h"
#include "menu.h"
#include "preferences.h"
#include "gl_compat.h"
#include "mat4.h"
#include "steam_build.h"
#include <iostream>
#include <string>

static const float SENSITIVITY_VALUES[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
static const char* SENSITIVITY_LABELS[] = {"SLOW", "LOW", "NORMAL", "HIGH", "MAX"};
static const int NUM_SENSITIVITY = 5;

static const float SMOOTHING_VALUES[] = {0.0f, 0.004f, 0.006f, 0.008f, 0.010f};
static const char* SMOOTHING_LABELS[] = {"OFF", "NORMAL", "HIGH", "HIGHER", "MAX"};
static const int NUM_SMOOTHING = 5;

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

static int smoothing_index_for(float value) {
  int best = 2;
  float best_dist = 1e6f;
  for (int i = 0; i < NUM_SMOOTHING; i++) {
    float d = value > SMOOTHING_VALUES[i] ? value - SMOOTHING_VALUES[i]
                                           : SMOOTHING_VALUES[i] - value;
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
  sensitivity_index_[0] = sensitivity_index_for(g_prefs.p1_keys.keyboard_sensitivity);
  sensitivity_index_[1] = sensitivity_index_for(g_prefs.p2_keys.keyboard_sensitivity);
  smoothing_index_[0]   = smoothing_index_for(g_prefs.p1_keys.camera_smoothing);
  smoothing_index_[1]   = smoothing_index_for(g_prefs.p2_keys.camera_smoothing);
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

  if (options_mode_) {
    Typer::draw_centered(0, 340, "OPTIONS", 28);

    static const int step_x5[] = {-200, -100, 0, 100, 200};

    // 4 rows: 0=P1 sens, 1=P1 smooth, 2=P2 sens, 3=P2 smooth
    static const int label_y[] = { 240,  80, -80, -240};
    static const int steps_y[] = { 195,  35, -125, -285};
    static const int name_y[]  = { 150, -10, -170, -330};
    static const char* row_names[] = {
      "P1  SENSITIVITY", "P1  SMOOTHING",
      "P2  SENSITIVITY", "P2  SMOOTHING"
    };

    for (int row = 0; row < 4; row++) {
      int p          = row / 2;
      bool is_smooth = (row % 2 == 1);
      int  num_steps = is_smooth ? NUM_SMOOTHING    : NUM_SENSITIVITY;
      const int *sx  = step_x5;
      int  cur_idx   = is_smooth ? smoothing_index_[p] : sensitivity_index_[p];
      const char* const *lbl = is_smooth ? SMOOTHING_LABELS : SENSITIVITY_LABELS;

      std::string heading = std::string(active_row_ == row ? "> " : "  ") + row_names[row];
      Typer::draw_centered(0, label_y[row], heading.c_str(), 13);

      for (int i = 0; i < num_steps; i++) {
        std::string step = (i == cur_idx)
          ? "[" + std::to_string(i + 1) + "]"
          :       std::to_string(i + 1);
        Typer::draw_centered(sx[i], steps_y[row], step.c_str(), 16);
      }
      Typer::draw_centered(0, name_y[row], lbl[cur_idx], 13);
    }
  } else {
    Typer::draw_centered(0, 320, "Newtonia", 80);
    if (high_score > 0) {
      Typer::draw_centered(0, -215, "HIGH SCORE", 14);
      Typer::draw_centered(0, -255, high_score, 18);
    }
  }

  if (!options_mode_) {
    if (attract_mode_ && !is_touch_mode()) {
      bool has_ctrl = false;
      int nc = SDL_NumJoysticks();
      for (int i = 0; i < nc; i++) {
        if (SDL_IsGameController(i)) { has_ctrl = true; break; }
      }
      if (!((currentTime / 1400) % 2)) {
        Typer::draw_centered(0, -50, has_ctrl ? "press start" : "press enter", 18);
      }
    } else if (quit_confirm_) {
      Typer::draw_centered(0, 50, "Quit?", 30);
      if (is_touch_mode()) {
        Typer::draw_centered(-Typer::scaled_window_width / 2, -50, "Yes", 26);
        Typer::draw_centered( Typer::scaled_window_width / 2, -50, "No",  26);
      } else {
        std::string yes_str = std::string(quit_selection_ == 0 ? "> " : "  ") + "Yes";
        std::string no_str  = std::string(quit_selection_ == 1 ? "> " : "  ") + "No";
        Typer::draw_centered(0,  -40, yes_str.c_str(), 22);
        Typer::draw_centered(0, -110, no_str.c_str(),  22);
      }
    } else if (has_save_) {
      if (is_touch_mode()) {
        // Side-by-side layout for touch: full left/right halves are tap targets
        Typer::draw_centered(-Typer::scaled_window_width / 2, -50, "CONTINUE", 26);
        Typer::draw_centered( Typer::scaled_window_width / 2, -50, "NEW GAME", 26);
      } else {
        // Stacked layout for keyboard/controller with selection indicator
        std::string cont    = std::string(menu_selection == 0 ? "> " : "  ") + "CONTINUE";
        std::string newgame = std::string(menu_selection == 1 ? "> " : "  ") + "NEW GAME";
        Typer::draw_centered(0,   80, cont.c_str(),    22);
        Typer::draw_centered(0,  -10, newgame.c_str(), 22);
        if (is_beta_feature_enabled()) {
          std::string options = std::string(menu_selection == 2 ? "> " : "  ") + "OPTIONS";
          Typer::draw_centered(0, -100, options.c_str(), 22);
        }
      }
    } else {
      if (is_touch_mode()) {
        if ((currentTime/1400) % 2) {
          Typer::draw_centered(0, -50, "tap to start", 18);
        }
      } else {
        // Keyboard/controller: show NEW GAME and OPTIONS
        std::string newgame = std::string(menu_selection == 0 ? "> " : "  ") + "NEW GAME";
        Typer::draw_centered(0,  -10, newgame.c_str(), 22);
        if (is_beta_feature_enabled()) {
          std::string options = std::string(menu_selection == 1 ? "> " : "  ") + "OPTIONS";
          Typer::draw_centered(0, -100, options.c_str(), 22);
        }
      }
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
        if (attract_mode_) {
          attract_mode_ = false;
          return;
        }
        if (options_mode_) {
          close_options();
        } else if (quit_confirm_) {
          if (quit_selection_ == 0) {
            glutLeaveMainLoop();
          } else {
            quit_confirm_ = false;
          }
        } else if (is_beta_feature_enabled() && menu_selection == max_menu_items() - 1) {
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
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
        if (active_row_ > 0) active_row_--;
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
        if (active_row_ < 3) active_row_++;
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
        adjust_active_row(-1);
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
        adjust_active_row(1);
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A ||
                 event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                 event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
                 event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
        close_options();
      }
    } else if (event.type == SDL_CONTROLLERAXISMOTION) {
      if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
        bool up   = event.caxis.value < -8000;
        bool down = event.caxis.value >  8000;
        if (up   && !left_stick_up_active   && active_row_ > 0) active_row_--;
        if (down && !left_stick_down_active && active_row_ < 3) active_row_++;
        left_stick_up_active   = up;
        left_stick_down_active = down;
      } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
        bool l = event.caxis.value < -8000;
        bool r = event.caxis.value >  8000;
        if (l && !left_stick_left_active)  adjust_active_row(-1);
        if (r && !left_stick_right_active) adjust_active_row(1);
        left_stick_left_active  = l;
        left_stick_right_active = r;
      }
    }
    return;
  }

  int n = max_menu_items();
  if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    if (attract_mode_) {
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A ||
          event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
        attract_mode_ = false;
      }
      return;
    }
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
#ifndef __EMSCRIPTEN__
      if (quit_confirm_) {
        quit_confirm_ = false;
      } else {
        quit_confirm_ = true;
        quit_selection_ = 0;
      }
#endif
    } else if (quit_confirm_) {
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
        quit_selection_ = 0;
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
        quit_selection_ = 1;
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
                 event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
        if (quit_selection_ == 0) {
          glutLeaveMainLoop();
        } else {
          quit_confirm_ = false;
        }
      }
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
      if (menu_selection > 0) menu_selection--;
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
      if (menu_selection < n - 1) menu_selection++;
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
      if (is_beta_feature_enabled() && menu_selection == n - 1) {
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
        if (quit_confirm_) {
          if (quit_selection_ == 0) {
            glutLeaveMainLoop();
          } else {
            quit_confirm_ = false;
          }
        } else if (is_beta_feature_enabled() && menu_selection == n - 1) {
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
      if (quit_confirm_) {
        if (up   && !left_stick_up_active)   quit_selection_ = 0;
        if (down && !left_stick_down_active) quit_selection_ = 1;
      } else {
        if (up   && !left_stick_up_active   && menu_selection > 0)     menu_selection--;
        if (down && !left_stick_down_active && menu_selection < n - 1) menu_selection++;
      }
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
    if (key == 'w' || key == 'W') {
      if (active_row_ > 0) active_row_--;
    } else if (key == 's' || key == 'S') {
      if (active_row_ < 3) active_row_++;
    } else if (key == 'a' || key == 'A') {
      adjust_active_row(-1);
    } else if (key == 'd' || key == 'D') {
      adjust_active_row(1);
    } else if (key == 27 || key == ' ' || key == '\r' || key == '\n') {
      close_options();
    }
    return;
  }

  int n = max_menu_items();

#if defined(__ANDROID__) || defined(__IOS__)
  // Touch/mobile — touch_tap and back_pressed() handle interaction.
  if (quit_confirm_) return;
  // suppress \r so a finger-down on the left (joystick) half doesn't immediately
  // confirm before the user lifts their finger.
  if (has_save_ && (key == '\r' || key == '\n')) return;
  if (is_beta_feature_enabled() && menu_selection == n - 1) {
    open_options();
  } else {
    confirm_selection(nullptr);
  }
#elif defined(__EMSCRIPTEN__)
  if (is_touch_mode()) {
    // Touch web: same as mobile — touch_tap handles selection, suppress \r
    if (quit_confirm_) return;
    if (has_save_ && (key == '\r' || key == '\n')) return;
    if (is_beta_feature_enabled() && menu_selection == n - 1) {
      open_options();
    } else {
      EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
      confirm_selection(nullptr);
    }
  } else {
    // Keyboard web: w/s navigate, space/enter confirm
    if (attract_mode_) {
      if (key == ' ' || key == '\r' || key == '\n') attract_mode_ = false;
      return;
    }
    if (quit_confirm_) {
      if (key == 27) {
        quit_confirm_ = false;
      } else if (key == ' ' || key == '\r' || key == '\n') {
        if (quit_selection_ == 0) {
          glutLeaveMainLoop();
        } else {
          quit_confirm_ = false;
        }
      } else if (key == 'w' || key == 'W') {
        quit_selection_ = 0;
      } else if (key == 's' || key == 'S') {
        quit_selection_ = 1;
      }
    } else {
      if (key == ' ' || key == '\r' || key == '\n') {
        if (is_beta_feature_enabled() && menu_selection == n - 1) {
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
  }
#else
  if (attract_mode_) {
    if (key == ' ' || key == '\r' || key == '\n') {
      attract_mode_ = false;
    } else if (key == 27) {
      attract_mode_ = false;
      quit_confirm_ = true;
      quit_selection_ = 0;
    }
    return;
  }
  if (quit_confirm_) {
    if (key == 27) {
      quit_confirm_ = false;
    } else if (key == ' ' || key == '\r' || key == '\n') {
      if (quit_selection_ == 0) {
        glutLeaveMainLoop();
      } else {
        quit_confirm_ = false;
      }
    } else if (key == 'w' || key == 'W') {
      quit_selection_ = 0;
    } else if (key == 's' || key == 'S') {
      quit_selection_ = 1;
    }
  } else {
    if (key == 27) {
      quit_confirm_ = true;
      quit_selection_ = 0;
    } else if (key == ' ' || key == '\r' || key == '\n') {
      if (is_beta_feature_enabled() && menu_selection == n - 1) {
        open_options();
      } else {
        confirm_selection(nullptr);
      }
    } else if (key == 'w' || key == 'W') {
      if (menu_selection > 0) menu_selection--;
    } else if (key == 's' || key == 'S') {
      if (menu_selection < n - 1) menu_selection++;
    }
  }
#endif
}

bool Menu::back_pressed() {
  if (quit_confirm_) {
    quit_confirm_ = false; // dismiss = No
    return true;
  }
  quit_confirm_ = true;
  quit_selection_ = 0;
  return true;
}

void Menu::touch_tap(float nx, float ny) {
  if (quit_confirm_) {
    // Left half = Yes (quit), right half = No (dismiss)
    if (nx < 0.5f) {
      glutLeaveMainLoop();
    } else {
      quit_confirm_ = false;
    }
    return;
  }
  if (!has_save_) return;
  // Left half = CONTINUE, right half = NEW GAME
  menu_selection = (nx >= 0.5f) ? 1 : 0;
#ifdef __EMSCRIPTEN__
  EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
  confirm_selection(nullptr);
}

int Menu::max_menu_items() const {
  if (is_beta_feature_enabled())
    return has_save_ ? 3 : 2;
  return has_save_ ? 2 : 1;
}

void Menu::open_options() {
  options_mode_ = true;
}

void Menu::adjust_active_row(int delta) {
  int p          = active_row_ / 2;
  bool is_smooth = (active_row_ % 2 == 1);
  if (is_smooth) {
    int &idx = smoothing_index_[p];
    idx += delta;
    if (idx < 0)              idx = 0;
    if (idx >= NUM_SMOOTHING) idx = NUM_SMOOTHING - 1;
  } else {
    int &idx = sensitivity_index_[p];
    idx += delta;
    if (idx < 0)               idx = 0;
    if (idx >= NUM_SENSITIVITY) idx = NUM_SENSITIVITY - 1;
  }
}

void Menu::close_options() {
  g_prefs.p1_keys.keyboard_sensitivity = SENSITIVITY_VALUES[sensitivity_index_[0]];
  g_prefs.p2_keys.keyboard_sensitivity = SENSITIVITY_VALUES[sensitivity_index_[1]];
  g_prefs.p1_keys.camera_smoothing     = SMOOTHING_VALUES[smoothing_index_[0]];
  g_prefs.p2_keys.camera_smoothing     = SMOOTHING_VALUES[smoothing_index_[1]];
  save_preferences();
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
