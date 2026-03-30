#include "typer.h"
#include "highscore.h"
#include "glstarfield.h"
#include "glgame.h"
#include "menu.h"
#include "gl_compat.h"
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

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(90, window.x()/window.y(), 100.0f, 2000.0f);
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(0, 0, window.x(), window.y());

  glTranslatef(-viewpoint.x(), -viewpoint.y(), 0.0f);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);
  glTranslatef(default_world_width, 0, 0.0f);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);
  glTranslatef(-default_world_width*2, 0, 0.0f);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);
  glTranslatef(default_world_width, 0, 0.0f);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);

  glTranslatef(viewpoint.x(), viewpoint.y(), 0.0f);
  Typer::draw_centered(0, 200, "Newtonia", 80);
  if (high_score > 0) {
    Typer::draw_centered(0, -215, "HIGH SCORE", 14);
    Typer::draw_centered(0, -255, high_score, 18);
  }

  if (has_save_) {
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
        confirm_selection(ctrl);
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
      glutLeaveMainLoop();
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
#ifdef __EMSCRIPTEN__
        EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
        confirm_selection(SDL_GameControllerFromInstanceID(event.caxis.which));
      }
      if(!pressed) r2_active = false;
    }
  }
}

void Menu::keyboard(unsigned char key, int x, int y) {
}

void Menu::keyboard_up(unsigned char key, int x, int y) {
#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__)
  // Touch/mobile/web — any key starts the game (touch_tap handles Continue/New Game)
#ifdef __EMSCRIPTEN__
  EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
  confirm_selection(nullptr);
#else
  if (key == 27) {
    glutLeaveMainLoop();
  } else if (key == ' ' || key == '\r' || key == '\n') {
    confirm_selection(nullptr);
  } else if (has_save_ && (key == 'w' || key == 'W')) {
    menu_selection = 0;
  } else if (has_save_ && (key == 's' || key == 'S')) {
    menu_selection = 1;
  }
#endif
}

void Menu::touch_tap(float nx, float ny) {
  if (!has_save_) return;
  // Left half = CONTINUE, right half = NEW GAME
  menu_selection = (nx >= 0.5f) ? 1 : 0;
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
