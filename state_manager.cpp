#include "state_manager.h"
#include "glgame.h"
#include "intro.h"
#include "menu.h"
#include "net_lobby.h"
#include "replay.h"
#include <SDL.h>
#include <string>

StateManager::StateManager() {
  // REPLAY.md R2 dev/test entry (the REPLAYS menu is R3): boot straight
  // into playback of a named file — or the shorthand current/recent/best.
  // Falls back to the menu (with a log line) when the file declines.
  const char *rp = SDL_getenv("NEWTONIA_REPLAY_PLAY");
  if (rp && *rp) {
    std::string path = rp;
    if (path == "current")     path = Replay::current_path();
    else if (path == "recent") path = Replay::recent_path();
    else if (path == "best")   path = Replay::best_path();
    else if (path == "last") {
      // "The last thing I played": the live/resumable run if one exists,
      // otherwise the most recently completed one — a finished run rotates
      // current -> recent, so plain `current` finds nothing after game over.
      Replay::Header h;
      path = Replay::read_header(Replay::current_path(), h)
                 ? Replay::current_path()
                 : Replay::recent_path();
    }
    if (State *s = GLGame::start_replay_playback(path)) {
      state = s;
      return;
    }
  }
  state = new Menu();
}

StateManager::~StateManager() {
  delete state;
}

// Frame-profiling counters (see gles2_compat.h). Zeroed HERE because every
// platform's main loop funnels through StateManager::draw — resetting only
// in glut.cpp left them growing unbounded (eventually signed-overflow UB)
// on web/Android/Xbox, whose loops call this directly. The desktop frame
// logger snapshots them right after its game->draw() returns.
extern int g_gles2_dbg_draws;
extern int g_gles2_dbg_line_segs;

void StateManager::draw() {
  g_gles2_dbg_draws = 0;
  g_gles2_dbg_line_segs = 0;
  state->draw();
}

void StateManager::mouse_move(int x, int y) {
  state->mouse_move(x, y);
}

void StateManager::keyboard(unsigned char key, int x, int y) {
  if(!key_states[key]) {
    key_states[key] = true;
    state->keyboard(key, x, y);
  }
}

void StateManager::keyboard_up(unsigned char key, int x, int y) {
  state->keyboard_up(key, x, y);
  key_states[key] = false;
}

void StateManager::controller(SDL_Event event) {
  state->controller(event);
}

void StateManager::controller_added(SDL_GameController *ctrl) {
  SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctrl));
  for(int i = 0; i < 2; i++) {
    if(active_controllers[i] == NULL) {
      active_controllers[i] = ctrl;
      active_controller_ids[i] = id;
      break;
    }
  }
  GLGame *game = dynamic_cast<GLGame*>(state);
  if(game) game->controller_added(ctrl);
  Intro *intro = dynamic_cast<Intro*>(state);
  if(intro) intro->controller_added(ctrl);
}

void StateManager::controller_removed(SDL_JoystickID id) {
  for(int i = 0; i < 2; i++) {
    if(active_controller_ids[i] == id) {
      active_controllers[i] = NULL;
      active_controller_ids[i] = -1;
      break;
    }
  }
  GLGame *game = dynamic_cast<GLGame*>(state);
  if(game) game->controller_removed(id);
  Intro *intro = dynamic_cast<Intro*>(state);
  if(intro) intro->controller_removed(id);
}

void StateManager::tick(int delta) {
  if(state->is_finished()) {
    State* next_state = state->get_next_state();
    // A state handed back to the manager (the game returning from an intro)
    // still carries the transition it requested; clear it before reinstalling.
    next_state->clear_state_change();
    next_state->resize(window.x(), window.y());
    // A finished state whose ownership moved to the next state (the game
    // adopted by an intro) must survive the transition.
    if(!state->ownership_transferred())
      delete state;
    state = next_state;
    GLGame *game = dynamic_cast<GLGame*>(state);
    if(game) {
      for(int i = 0; i < 2; i++) {
        if(active_controllers[i]) game->controller_added(active_controllers[i]);
      }
    }
  }
  state->tick(delta);
}

void StateManager::resize(int x, int y) {
  window = Point(x,y);
  state->resize(x,y);
  Typer::resize(x,y);
}

void StateManager::touch_joystick(float nx, float ny) {
  GLGame *game = dynamic_cast<GLGame*>(state);
  if(game) game->touch_joystick(nx, ny);
}

void StateManager::touch_tap(float nx, float ny) {
  state->touch_tap(nx, ny);
}

// Beta-only skip corner (a working skip in a normal game would let a stray
// tap skip the level and cheat-flag the run). The skip handler lives in
// keyboard_up (GLGame/Intro), so synthesize the full press+release — a bare
// key-down never actually skipped.
bool StateManager::debug_skip_corner_tap(float nx, float ny) {
  static const bool enabled = SDL_getenv("NEWTONIA_BETA") != NULL;
  if (!enabled || nx <= 0.85f || ny >= 0.15f) return false;
  keyboard('n', 0, 0);
  keyboard_up('n', 0, 0);
  return true;
}

bool StateManager::wants_background_ticks() {
  GLGame *game = dynamic_cast<GLGame*>(state);
  if (game) return game->net_active();
  return dynamic_cast<NetLobby*>(state) != NULL;
}

bool StateManager::back_pressed() {
  return state->back_pressed();
}

void StateManager::focus_lost() {
  key_states.clear();
  GLGame *game = dynamic_cast<GLGame*>(state);
  Intro *intro = dynamic_cast<Intro*>(state);
  if(game) {
    game->focus_lost();
  } else if(intro) {
    intro->focus_lost();
  } else if(!focus_muted) {
    Mix_PauseMusic();
    focus_muted = true;
  }
}

void StateManager::focus_gained() {
  GLGame *game = dynamic_cast<GLGame*>(state);
  Intro *intro = dynamic_cast<Intro*>(state);
  if(game) {
    game->focus_gained();
  } else if(intro) {
    intro->focus_gained();
  } else if(focus_muted) {
    Mix_ResumeMusic();
  }
  focus_muted = false;
}
