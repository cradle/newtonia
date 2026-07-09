#include "typer.h"
#include "asset_path.h"
#include "highscore.h"
#include "glstarfield.h"
#include "glgame.h"
#include "menu.h"
#include "net_lobby.h"
#include "net_transport.h"
#include "preferences.h"
#include "gl_compat.h"
#include "mat4.h"
#include "steam_build.h"
#include "view/overlay.h"
#include "view/tap_band.h"
#include <iostream>
#include <string>

static const float SENSITIVITY_VALUES[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
static const char* SENSITIVITY_LABELS[] = {"SLOW", "LOW", "NORMAL", "HIGH", "MAX"};
static const int NUM_SENSITIVITY = 5;

static const float SMOOTHING_VALUES[] = {0.0f, 0.004f, 0.006f, 0.008f, 0.010f};
static const char* SMOOTHING_LABELS[] = {"OFF", "NORMAL", "HIGH", "HIGHER", "MAX"};
static const int NUM_SMOOTHING = 5;

static const float STAR_DENSITY_MULTIPLIERS[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
static const char* STAR_DENSITY_LABELS[] = {"MINIMAL", "SPARSE", "MEDIUM", "MANY", "FULL"};
static const int NUM_STAR_DENSITY = 5;

// Per-player camera: index 0 = FIXED (view locked to the world), 1 = ROTATE
// (view follows the ship's heading). Stored as PlayerKeys::rotate_view.
static const char* CAMERA_LABELS[] = {"FIXED", "ROTATE"};
static const int NUM_CAMERA = 2;

// The Options screen rows, in display order. kind: 0=sensitivity, 1=smoothing,
// 2=camera, 3=star density. P2 rows are desktop-only — mobile (touch) shows
// Player 1 plus the shared options. Options is desktop/controller-only today
// (see Menu::show_options_row), so the touch list is future-proofing.
namespace { struct OptRow { int kind; int player; const char *name; }; }
static const OptRow OPT_ROWS_DESKTOP[] = {
  {0, 0, "P1  SENSITIVITY"}, {1, 0, "P1  SMOOTHING"}, {2, 0, "P1  CAMERA"},
  {0, 1, "P2  SENSITIVITY"}, {1, 1, "P2  SMOOTHING"}, {2, 1, "P2  CAMERA"},
  {3, 0, "STAR  DENSITY"},
};
// Mobile shows Player 1 + shared options only, so the "P1" prefix is dropped.
static const OptRow OPT_ROWS_TOUCH[] = {
  {0, 0, "SENSITIVITY"}, {1, 0, "SMOOTHING"}, {2, 0, "CAMERA"},
  {3, 0, "STAR DENSITY"},
};
static int opt_row_count() {
  return is_touch_mode() ? (int)(sizeof(OPT_ROWS_TOUCH) / sizeof(OPT_ROWS_TOUCH[0]))
                         : (int)(sizeof(OPT_ROWS_DESKTOP) / sizeof(OPT_ROWS_DESKTOP[0]));
}
static const OptRow &opt_row(int r) {
  return is_touch_mode() ? OPT_ROWS_TOUCH[r] : OPT_ROWS_DESKTOP[r];
}

// Touch options row geometry — shared by the draw and the tap hit-test so a
// tap always lands on the row it appears on. Rows fill the band above the
// RETURN TO MENU strip; row i's vertical centre and the row a tap falls in
// both come from here.
static const float TOUCH_OPT_TOP = 250.0f, TOUCH_OPT_BOTTOM = -210.0f;
static int touch_opt_center(int i, int n) {
  float pitch = (TOUCH_OPT_TOP - TOUCH_OPT_BOTTOM) / n;
  return (int)(TOUCH_OPT_TOP - (i + 0.5f) * pitch);
}
static int touch_opt_row_at(float ny, int n) {
  float y = (1.0f - 2.0f * ny) * Typer::scaled_window_height;
  float pitch = (TOUCH_OPT_TOP - TOUCH_OPT_BOTTOM) / n;
  float t = TOUCH_OPT_TOP - y;
  if (t < 0 || t >= pitch * n) return -1;
  return (int)(t / pitch);
}

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

static int star_density_index_for(float value) {
  int best = NUM_STAR_DENSITY - 1;
  float best_dist = 1e6f;
  for (int i = 0; i < NUM_STAR_DENSITY; i++) {
    float d = value > STAR_DENSITY_MULTIPLIERS[i] ? value - STAR_DENSITY_MULTIPLIERS[i]
                                                   : STAR_DENSITY_MULTIPLIERS[i] - value;
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
  starfield(new GLStarfield(Point(default_world_width, default_world_height), star_density_scale())) {
  sensitivity_index_[0] = sensitivity_index_for(g_prefs.p1_keys.keyboard_sensitivity);
  sensitivity_index_[1] = sensitivity_index_for(g_prefs.p2_keys.keyboard_sensitivity);
  smoothing_index_[0]   = smoothing_index_for(g_prefs.p1_keys.camera_smoothing);
  smoothing_index_[1]   = smoothing_index_for(g_prefs.p2_keys.camera_smoothing);
  camera_index_[0]      = g_prefs.p1_keys.rotate_view ? 1 : 0;
  camera_index_[1]      = g_prefs.p2_keys.rotate_view ? 1 : 0;
  star_density_index_   = star_density_index_for(g_prefs.star_density);
#ifdef __EMSCRIPTEN__
  EM_ASM(if (window.setMenuMode) window.setMenuMode(1););
#endif
  if(music == NULL) {
    music = Mix_LoadMUS(asset_path("audio/title.wav").c_str());
    if(music == NULL) {
      std::cout << "Unable to load title.wav (" << Mix_GetError() << ")" << std::endl;
    } else {
      Mix_PlayMusic(music, -1);
    }
  }
}

Menu::~Menu() {
  delete starfield;
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
  starfield->draw_front(viewpoint);
  starfield->draw_rear(viewpoint);

  mat4_translate(vp, proj, -viewpoint.x() + default_world_width, -viewpoint.y(), 0.0f);
  gles2_set_vp(vp);
  starfield->draw_front(viewpoint);
  starfield->draw_rear(viewpoint);

  mat4_translate(vp, proj, -viewpoint.x() - default_world_width, -viewpoint.y(), 0.0f);
  gles2_set_vp(vp);
  starfield->draw_front(viewpoint);
  starfield->draw_rear(viewpoint);

  // Ortho overlay for text (identity model — Typer applies its own transforms via pre_draw).
  // Extents are widened by SAFE_AREA_SCALE so menu text stays inside the
  // TV title-safe region on Xbox (no-op elsewhere).
  float menu_hw = window.x() / Overlay::SAFE_AREA_SCALE;
  float menu_hh = window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -menu_hw, menu_hw, -menu_hh, menu_hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  if (options_mode_) {
    bool touch = is_touch_mode();
    Typer::draw_centered(0, touch ? 340 : 368, "OPTIONS", touch ? 30 : 26);

    int n = opt_row_count();
    // Desktop: three centred lines per row (heading / step marks / value),
    // pitched to fit up to 7 rows. The options screen has no copyright line,
    // so the rows use the full height below the header for breathing room.
    // Touch: one big tappable row per option, name on the left and the
    // current value on the right (tap to cycle).
    const float band_top = 305.0f, band_bottom = -480.0f;
    float pitch = (band_top - band_bottom) / n;
    int within = 34;  // desktop heading↔steps↔name spacing

    for (int row = 0; row < n; row++) {
      const OptRow &r = opt_row(row);

      int num_steps, cur_idx;
      const char* const *lbl;
      switch (r.kind) {
        case 0: num_steps = NUM_SENSITIVITY;  cur_idx = sensitivity_index_[r.player]; lbl = SENSITIVITY_LABELS;   break;
        case 1: num_steps = NUM_SMOOTHING;    cur_idx = smoothing_index_[r.player];   lbl = SMOOTHING_LABELS;     break;
        case 2: num_steps = NUM_CAMERA;       cur_idx = camera_index_[r.player];      lbl = CAMERA_LABELS;        break;
        default:num_steps = NUM_STAR_DENSITY; cur_idx = star_density_index_;          lbl = STAR_DENSITY_LABELS;  break;
      }

      if (touch) {
        int cy = touch_opt_center(row, n);
        // Name left, value right. Name font sized so the longest label
        // ("SENSITIVITY"/"STAR DENSITY") clears the value column.
        Typer::draw(-315, cy, r.name, 16);               // name, left-aligned
        Typer::draw_centered(205, cy, lbl[cur_idx], 18); // value
        continue;
      }

      int center    = (int)(band_top - (row + 0.5f) * pitch);
      std::string heading = std::string(active_row_ == row ? "> " : "  ") + r.name;
      Typer::draw_centered(0, center + within, heading.c_str(), 12);
      // Step marks centred on 0, 100 apart (5 steps span -200..200; the
      // 2-step camera row sits at -50/50).
      for (int i = 0; i < num_steps; i++) {
        int x = (int)((i - (num_steps - 1) * 0.5f) * 100);
        std::string step = (i == cur_idx)
          ? "[" + std::to_string(i + 1) + "]"
          :       std::to_string(i + 1);
        Typer::draw_centered(x, center, step.c_str(), 14);
      }
      Typer::draw_centered(0, center - within, lbl[cur_idx], 11);
    }

    if (touch) {
      Typer::draw_centered(0, -270, "TAP AN OPTION TO CHANGE IT", 12);
      TapBand::return_to_menu.draw("RETURN TO MENU", currentTime);
    }
  } else {
    Typer::draw_centered(0, 320, "Newtonia", 80);
    if (high_score > 0) {
      // Touch: the menu rows spread over a taller band (bigger finger
      // targets), so the score block sits lower, above the copyright.
      int hs_y = is_touch_mode() ? -330 : -215;
      Typer::draw_centered(0, hs_y, "HIGH SCORE", 14);
      Typer::draw_centered(0, hs_y - 40, high_score, 18);
    }
  }

  if (!options_mode_) {
    if (attract_mode_) {
      if (!((currentTime / 1400) % 2)) {
        // title_bot=160 (320-2*80), scores_top=-215; center single item in that gap
        const int sz = 18, h = 2 * sz;
        int gap = (160 - (-215) - h) / 2;
        if (is_touch_mode()) {
          Typer::draw_centered(0, 160 - gap, "tap to start", sz);
        } else {
          bool has_ctrl = false;
          int nc = SDL_NumJoysticks();
          for (int i = 0; i < nc; i++) {
            if (SDL_IsGameController(i)) { has_ctrl = true; break; }
          }
          Typer::draw_centered(0, 160 - gap, has_ctrl ? "press start" : "press enter", sz);
        }
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
    } else if (new_confirm_) {
      Typer::draw_centered(0, 50, "New game?", 30);
      if (is_touch_mode()) {
        Typer::draw_centered(-Typer::scaled_window_width / 2, -50, "YES", 26);
        Typer::draw_centered( Typer::scaled_window_width / 2, -50, "NO",  26);
      } else {
        std::string yes_str = std::string(new_selection_ == 0 ? "> " : "  ") + "YES";
        std::string no_str  = std::string(new_selection_ == 1 ? "> " : "  ") + "NO";
        Typer::draw_centered(0,  -40, yes_str.c_str(), 22);
        Typer::draw_centered(0, -110, no_str.c_str(),  22);
      }
    } else if (has_save_) {
      std::vector<std::string> rows;
      rows.push_back("CONTINUE");
      rows.push_back("NEW GAME");
      if (show_online_row()) rows.push_back("ONLINE");
      if (show_options_row()) rows.push_back("OPTIONS");
      draw_menu_rows(rows);
    } else {
      std::vector<std::string> rows;
      rows.push_back("NEW GAME");
      if (show_online_row()) rows.push_back("ONLINE");
      if (show_options_row()) rows.push_back("OPTIONS");
      draw_menu_rows(rows);
    }
  }
  if (!options_mode_)
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
        } else if (new_confirm_) {
          if (new_selection_ == 0) {
            confirm_selection(ctrl);
          } else {
            new_confirm_ = false;
          }
        } else if (show_options_row() && menu_selection == max_menu_items() - 1) {
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
        if (active_row_ < opt_row_count() - 1) active_row_++;
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
        if (down && !left_stick_down_active && active_row_ < opt_row_count() - 1) active_row_++;
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
#ifndef __EMSCRIPTEN__
      else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
        attract_mode_ = false;
        quit_confirm_ = true;
        quit_selection_ = 0;
      }
#endif
      return;
    }
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
      if (new_confirm_) {
        new_confirm_ = false; // dismiss = No
        return;
      }
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
    } else if (new_confirm_) {
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
        new_selection_ = 0;
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
        new_selection_ = 1;
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
                 event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
        if (new_selection_ == 0) {
          confirm_selection(SDL_GameControllerFromInstanceID(event.cbutton.which));
        } else {
          new_confirm_ = false;
        }
      } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
        new_confirm_ = false;
      }
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
      if (menu_selection > 0) menu_selection--;
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
      if (menu_selection < n - 1) menu_selection++;
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
      if (show_options_row() && menu_selection == n - 1) {
        open_options();
      } else {
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
        } else if (new_confirm_) {
          if (new_selection_ == 0) {
            confirm_selection(SDL_GameControllerFromInstanceID(event.caxis.which));
          } else {
            new_confirm_ = false;
          }
        } else if (show_options_row() && menu_selection == n - 1) {
          open_options();
        } else {
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
      } else if (new_confirm_) {
        if (up   && !left_stick_up_active)   new_selection_ = 0;
        if (down && !left_stick_down_active) new_selection_ = 1;
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
      if (active_row_ < opt_row_count() - 1) active_row_++;
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
  // Touch/mobile — touch_tap and back_pressed() handle ALL interaction
  // (attract dismissal, row selection, confirms). The keys arriving here
  // are only the ones the touch zones synthesize (\r, space, x, p);
  // acting on them would double-handle the tap touch_tap already did.
  (void)n;
  (void)key;
  return;
#elif defined(__EMSCRIPTEN__)
  if (is_touch_mode()) {
    // Touch web: same as mobile — touch_tap owns all interaction and
    // web_menu_tap() synthesizes a stray keyboard_up('\r') per tap.
    return;
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
    } else if (new_confirm_) {
      if (key == 27) {
        new_confirm_ = false;
      } else if (key == ' ' || key == '\r' || key == '\n') {
        if (new_selection_ == 0) {
          confirm_selection(nullptr);
        } else {
          new_confirm_ = false;
        }
      } else if (key == 'w' || key == 'W') {
        new_selection_ = 0;
      } else if (key == 's' || key == 'S') {
        new_selection_ = 1;
      }
    } else {
      if (key == ' ' || key == '\r' || key == '\n') {
        if (show_options_row() && menu_selection == n - 1) {
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
  } else if (new_confirm_) {
    if (key == 27) {
      new_confirm_ = false;
    } else if (key == ' ' || key == '\r' || key == '\n') {
      if (new_selection_ == 0) {
        confirm_selection(nullptr);
      } else {
        new_confirm_ = false;
      }
    } else if (key == 'w' || key == 'W') {
      new_selection_ = 0;
    } else if (key == 's' || key == 'S') {
      new_selection_ = 1;
    }
  } else {
    if (key == 27) {
      quit_confirm_ = true;
      quit_selection_ = 0;
    } else if (key == ' ' || key == '\r' || key == '\n') {
      if (show_options_row() && menu_selection == n - 1) {
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
  if (options_mode_) {
    close_options();  // persists and returns to the menu
    return true;
  }
  if (attract_mode_) {
    attract_mode_ = false;
#if defined(_GAMING_XBOX) || defined(_GAMING_DESKTOP)
    quit_confirm_ = true;
    quit_selection_ = 0;
#endif
    return true;
  }
  if (new_confirm_) {
    new_confirm_ = false; // dismiss = No, keep the save
    return true;
  }
  if (quit_confirm_) {
    quit_confirm_ = false; // dismiss = No
    return true;
  }
  quit_confirm_ = true;
  quit_selection_ = 0;
  return true;
}

void Menu::touch_tap(float nx, float ny) {
  if (attract_mode_) {
    attract_mode_ = false;  // any tap dismisses the attract screen
    return;
  }
  if (options_mode_) {
    // Bottom strip exits (and persists via close_options); tapping a row
    // cycles that option to its next value, wrapping at the end.
    if (TapBand::return_to_menu.contains(nx, ny)) { close_options(); return; }
    int row = touch_opt_row_at(ny, opt_row_count());
    if (row >= 0) {
      active_row_ = row;
      adjust_active_row(+1, /*wrap=*/true);
    }
    return;
  }
  if (quit_confirm_) {
    // Left half = Yes (quit), right half = No (dismiss)
    if (nx < 0.5f) {
      glutLeaveMainLoop();
    } else {
      quit_confirm_ = false;
    }
    return;
  }
  if (new_confirm_) {
    // Left half = YES (wipe save, start fresh), right half = NO (keep save)
    if (nx < 0.5f) {
      confirm_selection(nullptr);
    } else {
      new_confirm_ = false;
    }
    return;
  }
  // Rows are laid out like the desktop menu; hit-test the tapped row.
  int row = menu_row_at(ny);
  if (row < 0) return;
  menu_selection = row;
  if (show_options_row() && row == max_menu_items() - 1) {
    open_options();
    return;
  }
  confirm_selection(nullptr);
}

int Menu::max_menu_items() const {
  int n = has_save_ ? 2 : 1;
  if (show_online_row()) n++;
  if (show_options_row()) n++;
  return n;
}

bool Menu::show_online_row() const {
  return net_available();
}

bool Menu::show_options_row() const {
  return true;  // always available (was beta-gated)
}

int Menu::menu_row_size() { return is_touch_mode() ? 26 : 22; }

// Bottom of the menu-row band: desktop packs rows above the high-score
// block; touch spreads them over a taller band (they're finger targets)
// and the score block moves down to make room.
static int menu_rows_bottom() { return is_touch_mode() ? -300 : -215; }

// Equally space n row blocks of height h between title_bot=160 and
// menu_rows_bottom(). ONE definition shared by draw_menu_rows and
// menu_row_at so taps always land on what is drawn.
static int menu_row_gap(int n, int h) {
  return (160 - menu_rows_bottom() - n * h) / (n + 1);
}

// Touch draws bigger glyphs and no selection cursor; menu_row_at() mirrors
// this exact geometry so taps land on what is drawn.
void Menu::draw_menu_rows(const std::vector<std::string> &rows) {
  const int sz = menu_row_size(), h = 2 * sz;
  int n = (int)rows.size();
  int gap = menu_row_gap(n, h);
  for (int i = 0; i < n; i++) {
    std::string row = rows[i];
    if (!is_touch_mode())
      row = std::string(menu_selection == i ? "> " : "  ") + row;
    Typer::draw_centered(0, 160 - (i + 1) * gap - i * h, row.c_str(), sz);
  }
}

int Menu::menu_row_at(float ny) const {
  const int sz = menu_row_size(), h = 2 * sz;
  int n = max_menu_items();
  int gap = menu_row_gap(n, h);
  // The menu ortho maps normalized tap y (0=top, 1=bottom) linearly onto
  // Typer virtual y in [+scaled_window_height, -scaled_window_height].
  float y = (1.0f - 2.0f * ny) * Typer::scaled_window_height;
  // Slot i covers its glyph block plus half a gap either side, so the
  // whole menu band is contiguous finger targets with no dead zones.
  float t = (160.0f - y) - gap * 0.5f;
  if (t < 0) return -1;
  int i = (int)(t / (gap + h));
  return i < n ? i : -1;
}

int Menu::online_row_index() const {
  if (!show_online_row()) return -1;
  return has_save_ ? 2 : 1;  // directly after NEW GAME
}

void Menu::open_options() {
  options_mode_ = true;
}

void Menu::adjust_active_row(int delta, bool wrap) {
  const OptRow &r = opt_row(active_row_);
  int *idx, num;
  switch (r.kind) {
    case 0: idx = &sensitivity_index_[r.player]; num = NUM_SENSITIVITY;  break;
    case 1: idx = &smoothing_index_[r.player];   num = NUM_SMOOTHING;    break;
    case 2: idx = &camera_index_[r.player];      num = NUM_CAMERA;       break;
    default:idx = &star_density_index_;          num = NUM_STAR_DENSITY; break;
  }
  *idx += delta;
  if (wrap) {
    // Touch cycles round; ((v % num) + num) % num handles either direction.
    *idx = ((*idx % num) + num) % num;
  } else {
    if (*idx < 0)    *idx = 0;
    if (*idx >= num) *idx = num - 1;
  }
}

void Menu::close_options() {
  g_prefs.p1_keys.keyboard_sensitivity = SENSITIVITY_VALUES[sensitivity_index_[0]];
  g_prefs.p2_keys.keyboard_sensitivity = SENSITIVITY_VALUES[sensitivity_index_[1]];
  g_prefs.p1_keys.camera_smoothing     = SMOOTHING_VALUES[smoothing_index_[0]];
  g_prefs.p2_keys.camera_smoothing     = SMOOTHING_VALUES[smoothing_index_[1]];
  g_prefs.p1_keys.rotate_view          = (camera_index_[0] == 1);
  g_prefs.p2_keys.rotate_view          = (camera_index_[1] == 1);
  g_prefs.star_density                 = STAR_DENSITY_MULTIPLIERS[star_density_index_];
  save_preferences();
  delete starfield;
  starfield = new GLStarfield(Point(default_world_width, default_world_height),
                              STAR_DENSITY_MULTIPLIERS[star_density_index_]);
  options_mode_ = false;
}

void Menu::confirm_selection(SDL_GameController *ctrl) {
  if (menu_selection == online_row_index()) {
    request_state_change(new NetLobby());
    return;
  }
  if (has_save_ && menu_selection == 0) {
    Save::GameState s;
    if (Save::load_game(s)) {
#ifdef __EMSCRIPTEN__
      EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
      request_state_change(new GLGame(s, ctrl));
      return;
    }
    // Corrupt or missing save — fall through to new game
    has_save_ = false;
  }
  // Starting fresh would wipe the existing save — ask first. The YES action
  // re-enters with new_confirm_ set and proceeds.
  if (has_save_ && !new_confirm_) {
    new_confirm_ = true;
    new_selection_ = 1;  // default to NO
    return;
  }
  new_confirm_ = false;
#ifdef __EMSCRIPTEN__
  EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
  request_state_change(new GLGame(ctrl));
}
