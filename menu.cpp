#include "typer.h"
#include "highscore.h"
#include "glstarfield.h"
#include "glgame.h"
#include "menu.h"
#include "gl_compat.h"
#include "mat4.h"
#include <iostream>
#include <string>

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

  Typer::draw_centered(0, 280, "Newtonia", 80);
  if (high_score > 0) {
    Typer::draw_centered(0, -215, "HIGH SCORE", 14);
    Typer::draw_centered(0, -255, high_score, 18);
  }

  if (quit_confirm_) {
    Typer::draw_centered(0, 50, "Quit?", 30);
    if (is_touch_mode()) {
      // Side-by-side layout for touch: left = Yes, right = No
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
      Typer::draw_centered(0,  -10, cont.c_str(),    22);
      Typer::draw_centered(0, -100, newgame.c_str(), 22);
    }
  } else {
    if((currentTime/1400) % 2) {
      if(is_touch_mode()) {
        Typer::draw_centered(0, -50, "tap to start", 18);
      } else if(SDL_NumJoysticks() == 0) {
        Typer::draw_centered(0, -50, "press enter", 18);
      } else {
        Typer::draw_centered(0, -50, "press start", 18);
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
        if (quit_confirm_) {
          if (quit_selection_ == 0) {
            glutLeaveMainLoop();
          } else {
            quit_confirm_ = false;
          }
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
  if(event.type == SDL_CONTROLLERBUTTONDOWN) {
    if(event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
      if (quit_confirm_) {
        quit_confirm_ = false;
      } else {
        quit_confirm_ = true;
        quit_selection_ = 0;
      }
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
    } else if(has_save_ && event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
      menu_selection = 0;
    } else if(has_save_ && event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
      menu_selection = 1;
    } else if(event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
#ifdef __EMSCRIPTEN__
      EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
      confirm_selection(SDL_GameControllerFromInstanceID(event.cbutton.which));
    }
  } else if(event.type == SDL_CONTROLLERAXISMOTION) {
    if(event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
      bool pressed = event.caxis.value > 8000;
      if(pressed && !r2_active) {
        r2_active = true;
        if (quit_confirm_) {
          if (quit_selection_ == 0) {
            glutLeaveMainLoop();
          } else {
            quit_confirm_ = false;
          }
        } else {
#ifdef __EMSCRIPTEN__
          EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
          confirm_selection(SDL_GameControllerFromInstanceID(event.caxis.which));
        }
      }
      if(!pressed) r2_active = false;
    } else if(event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
      bool up   = event.caxis.value < -8000;
      bool down = event.caxis.value >  8000;
      if (quit_confirm_) {
        if(up && !left_stick_up_active) quit_selection_ = 0;
        if(down && !left_stick_down_active) quit_selection_ = 1;
      } else if (has_save_) {
        if(up && !left_stick_up_active) menu_selection = 0;
        if(down && !left_stick_down_active) menu_selection = 1;
      }
      left_stick_up_active   = up;
      left_stick_down_active = down;
    }
  }
}

void Menu::keyboard(unsigned char key, int x, int y) {
}

void Menu::keyboard_up(unsigned char key, int x, int y) {
#if defined(__ANDROID__) || defined(__IOS__)
  // Touch/mobile — touch_tap and back_pressed() handle interaction.
  if (quit_confirm_) return;
  // suppress \r so a finger-down on the left (joystick) half doesn't immediately
  // confirm before the user lifts their finger.
  if (has_save_ && (key == '\r' || key == '\n')) return;
  confirm_selection(nullptr);
#elif defined(__EMSCRIPTEN__)
  if (is_touch_mode()) {
    // Touch web: same as mobile — touch_tap handles selection, suppress \r
    if (quit_confirm_) return;
    if (has_save_ && (key == '\r' || key == '\n')) return;
    EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
    confirm_selection(nullptr);
  } else {
    // Keyboard web: w/s navigate, space/enter confirm, ESC shows quit dialog
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
        EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
        confirm_selection(nullptr);
      } else if (key == 27) {
        quit_confirm_ = true;
        quit_selection_ = 0;
      } else if (has_save_ && (key == 'w' || key == 'W')) {
        menu_selection = 0;
      } else if (has_save_ && (key == 's' || key == 'S')) {
        menu_selection = 1;
      }
    }
  }
#else
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
      confirm_selection(nullptr);
    } else if (has_save_ && (key == 'w' || key == 'W')) {
      menu_selection = 0;
    } else if (has_save_ && (key == 's' || key == 'S')) {
      menu_selection = 1;
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
