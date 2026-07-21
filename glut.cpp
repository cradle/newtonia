// Desktop-only entry point. Android uses android_main.cpp; web uses web_main.cpp.
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

#include <stdlib.h> // For EXIT_SUCCESS
#include <time.h>   // For time()

#include <SDL.h>
#include <SDL_mixer.h>

#include "state_manager.h"
#include "asteroid.h"
#include "typer.h"
#include "preferences.h"
#include "net_transport.h"
#include "net_signal.h"
#include "achievements.h"
#include "presence.h"
#include "invites.h"

// gl_compat.h pulls in GLUT (for window management) and gles2_compat.h
// (for the VBO/VAO/shader shim that replaces all legacy GL calls).
#include "gl_compat.h"

#ifdef __APPLE__
// CGL is needed for VSync configuration only.
#include <OpenGL/OpenGL.h>
extern "C" void activate_app_macos();
extern "C" void enable_game_mode_macos();
extern "C" void set_native_fullscreen_macos(int want_fullscreen);
extern "C" void install_macos_focus_observer(void (*lost)(), void (*gained)());
#endif

#ifdef __linux__
// GLX lets us retrieve the X11 Display/drawable so we can poll keyboard focus.
#include <GL/glx.h>
#include <X11/Xlib.h>
#endif
// On Windows, <windows.h> is already pulled in by gl_compat.h.

// Glut callbacks cannot be member functions. Need to pre-declare game object
StateManager *game;

SDL_GameController *controllers[2] = {NULL, NULL};
SDL_JoystickID controller_ids[2] = {-1, -1};
bool ENABLE_AUDIO = true;

int last_render_time;
#ifdef __APPLE__
static bool s_needs_activation = true;
static int  s_activation_retries = 0;
// Set when the game launches with fullscreen saved in preferences.  We defer
// the native-fullscreen transition until the window is on screen (handled in
// draw()), since toggleFullScreen: is unreliable before the app has finished
// launching.
static bool s_needs_fullscreen = false;
void activate_app_timer(int);
void hide_cursor_after_fullscreen(int);
#endif

static int s_last_frame_draws = 0, s_last_frame_segs = 0;

void draw() {
  if (!game) return;
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  last_render_time = current_time;
  game->draw();  // StateManager::draw zeroes the dbg counters at entry
  s_last_frame_draws = g_gles2_dbg_draws;
  s_last_frame_segs  = g_gles2_dbg_line_segs;
  glutSwapBuffers();
  // A Steam join accepted while the game is already running (steam://run into
  // an already-open game) does not bring us to the front. Drain the request
  // each frame; on macOS re-run the activate/retry cycle so our window rises
  // above Steam. (Windows/Linux Steam focuses the game itself, so the drained
  // request is a harmless no-op there for now.)
  if (Invites::take_focus_request()) {
#ifdef __APPLE__
    s_needs_activation = true;
#endif
  }
#ifdef __APPLE__
  // Activate after the first rendered frame so the window is on screen before
  // we request focus (a 0ms timer fires before the window is visible), then
  // once more 200 ms later — twice total. activate_app_macos() re-raises the
  // window every call (orderFrontRegardless), so hammering it for seconds
  // yanks focus back if the user alt-tabs away right after launch; two quick
  // attempts get us in front without fighting the user after that.
  if (s_needs_activation) {
    s_needs_activation = false;
    s_activation_retries = 0;
    activate_app_macos();
    glutTimerFunc(200, activate_app_timer, 0);
  }
  // Enter the fullscreen Space once the window is actually on screen.
  if (s_needs_fullscreen) {
    s_needs_fullscreen = false;
    set_native_fullscreen_macos(1);
    glutTimerFunc(300, hide_cursor_after_fullscreen, 0);
  }
#endif
}

int old_x = 50;
int old_y = 50;
int old_width = 800;
int old_height = 600;
bool is_fullscreen = false;
bool cursor_hidden = false;

#ifdef __APPLE__
void hide_cursor_after_fullscreen(int);
#endif

void set_cursor_hidden(bool hide) {
  if (!hide && !cursor_hidden) return;
  cursor_hidden = hide;
  if (hide) {
#ifdef __APPLE__
    glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
#endif
    glutSetCursor(GLUT_CURSOR_NONE);
  } else {
    glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
  }
}

void keyboard(unsigned char key, int x, int y) {
  // The bare F toggle yields while a text field is consuming keystrokes
  // (the lobby's room-code entry): the Deck's floating keyboard typing F
  // flickered fullscreen. Alt+Enter still works — no keyboard types it.
  bool do_fullscreen =
      (key == (unsigned char)g_prefs.general_keys.toggle_fullscreen &&
       !game->text_entry_active())
    || (key == '\r' && glutGetModifiers() == GLUT_ACTIVE_ALT);
  if (do_fullscreen) {
    if (!is_fullscreen) {
      old_x = glutGet(GLUT_WINDOW_X);
      old_y = glutGet(GLUT_WINDOW_Y);
      old_width = glutGet(GLUT_WINDOW_WIDTH);
      old_height = glutGet(GLUT_WINDOW_HEIGHT);
      is_fullscreen = true;
#ifdef __APPLE__
      // Use a real fullscreen Space so macOS 14+ can engage Game Mode.
      set_native_fullscreen_macos(1);
      glutTimerFunc(300, hide_cursor_after_fullscreen, 0);
#else
      glutFullScreen();
      set_cursor_hidden(true);
#endif
    } else {
      is_fullscreen = false;
      set_cursor_hidden(false);
#ifdef __APPLE__
      // Leaving the fullscreen Space restores the previous window frame.
      set_native_fullscreen_macos(0);
#else
      glutPositionWindow(old_x, old_y);
      glutReshapeWindow(old_width, old_height);
#endif
    }
    g_prefs.fullscreen = is_fullscreen;
    save_preferences();
  }
  game->keyboard(key, x, y);
}

void special(int key, int x, int y) {
  switch (key) {
    case GLUT_KEY_F4:
      if(glutGetModifiers() == GLUT_ACTIVE_ALT) {
        glutLeaveMainLoop();
      }
      break;
  }
  keyboard(key+128, x, y);
}

void keyboard_up(unsigned char key, int x, int y) {
  bool is_fullscreen_key =
      (key == (unsigned char)g_prefs.general_keys.toggle_fullscreen &&
       !game->text_entry_active())
    || (key == '\r' && glutGetModifiers() == GLUT_ACTIVE_ALT);
  if (!is_fullscreen_key)
    game->keyboard_up(key, x, y);
}

void special_up(int key, int x, int y) {
  keyboard_up(key+128, x, y);
}

void resize(int width, int height) {
  Typer::resize(width, height);
  if (game) game->resize(width, height);
  if (!is_fullscreen) {
    g_prefs.window_width  = width;
    g_prefs.window_height = height;
    save_preferences();
  }
#ifndef __APPLE__
  set_cursor_hidden(is_fullscreen);
#endif
}

#ifdef __APPLE__
void hide_cursor_after_fullscreen(int) {
  if (is_fullscreen) {
    cursor_hidden = false;
    set_cursor_hidden(true);
  }
}

void activate_app_timer(int) {
  activate_app_macos(); // No-op once [NSApp isActive].
  // One retry only: this is the SECOND (and final) activation — the first ran
  // in draw() when the window first appeared. Two attempts then stop, so we
  // never fight the user for focus after launch.
  if (++s_activation_retries < 1) {
    glutTimerFunc(200, activate_app_timer, 0);
  }
}

void mouse_passive(int x, int y) {
  if (!is_fullscreen) return;
  cursor_hidden = false;
  set_cursor_hidden(true);
}

static void on_focus_lost()   { if (game) game->focus_lost(); }
static void on_focus_gained() { if (game) game->focus_gained(); }
#endif // __APPLE__


void check_controller() {
  SDL_Event e;
  while(SDL_PollEvent(&e)) {
    if(e.type == SDL_QUIT) {
      // SDL owns no window on desktop (GLUT does), so SDL_QUIT here means a
      // caught SIGINT/SIGTERM — SDL's event subsystem translates both into
      // this event instead of letting them kill the process. Leave through
      // the same clean path as Alt-F4: glutLeaveMainLoop() (freeglut) /
      // exit(0) (macOS shim), so the save + presence/invites/Steam teardown
      // runs. Unhandled, the event was silently dropped and `kill` never
      // stopped the game.
      std::cout << "SDL_QUIT received - shutting down" << std::endl;
      glutLeaveMainLoop();
      return;
    }
    if(e.type == SDL_CONTROLLERDEVICEADDED) {
      for(int i = 0; i < 2; i++) {
        if(controllers[i] == NULL) {
          controllers[i] = SDL_GameControllerOpen(e.cdevice.which);
          if(controllers[i]) {
            controller_ids[i] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i]));
            std::cout << "Controller " << i+1 << " connected: " << SDL_GameControllerName(controllers[i]) << std::endl;
            game->controller_added(controllers[i]);
          }
          break;
        }
      }
    } else if(e.type == SDL_CONTROLLERDEVICEREMOVED) {
      SDL_JoystickID removed_id = e.cdevice.which;
      for(int i = 0; i < 2; i++) {
        if(controller_ids[i] == removed_id) {
          SDL_GameControllerClose(controllers[i]);
          controllers[i] = NULL;
          controller_ids[i] = -1;
          std::cout << "Controller " << i+1 << " disconnected" << std::endl;
          game->controller_removed(removed_id);
          break;
        }
      }
    }
    game->controller(e);
  }
}

#ifdef __linux__
// Poll X11 keyboard focus and fire focus_lost/gained on the game when it
// changes.  GLUT owns the window on desktop so SDL_WINDOWEVENT never fires;
// querying X11 directly via the current GLX display/drawable is the only
// portable way to detect focus transitions on Linux.
static bool linux_has_focus = true;

static void check_linux_focus() {
  Display *dpy = glXGetCurrentDisplay();
  if (!dpy) return;

  Window focused;
  int revert;
  XGetInputFocus(dpy, &focused, &revert);

  Window glut_win = (Window)glXGetCurrentDrawable();
  bool has_focus = (focused == glut_win);

  if (!has_focus && linux_has_focus) {
    linux_has_focus = false;
    if (game) game->focus_lost();
  } else if (has_focus && !linux_has_focus) {
    linux_has_focus = true;
    if (game) game->focus_gained();
  }
}
#endif // __linux__

#ifdef _WIN32
// GetActiveWindow() returns our HWND when this thread's window has focus,
// NULL when another application is in the foreground.
static bool windows_has_focus = true;

static void check_windows_focus() {
  bool has_focus = (GetActiveWindow() != NULL);
  if (!has_focus && windows_has_focus) {
    windows_has_focus = false;
    if (game) game->focus_lost();
  } else if (has_focus && !windows_has_focus) {
    windows_has_focus = true;
    if (game) game->focus_gained();
  }
}
#endif // _WIN32

int last_tick_time;
void tick() {
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  int delta = current_time - last_tick_time;
  last_tick_time = current_time;
  // NEWTONIA_FRAME_LOG=1: log every frame slower than 50 ms (sim + draw +
  // swap, since tick idles between redisplays). Greppable in headless runs
  // and cheap enough to leave in — field reports like "frame rate collapsed
  // when the mines went off" become measurable instead of anecdotal.
  static const bool frame_log = getenv("NEWTONIA_FRAME_LOG") != NULL;
  if (frame_log && delta > 50)
    std::cout << "frame: " << delta << " ms at t=" << current_time
              << " draws=" << s_last_frame_draws
              << " segs=" << s_last_frame_segs << std::endl;
  check_controller();
#ifdef __linux__
  check_linux_focus();
#endif
#ifdef _WIN32
  check_windows_focus();
#endif
  steam_run_callbacks();
  game->tick(delta);
  glutPostRedisplay();
}

void isVisible(int state) {
  if(state == GLUT_VISIBLE) {
    last_render_time = last_tick_time = glutGet(GLUT_ELAPSED_TIME);
    glutVisibilityFunc(NULL);
    glutIdleFunc(tick);
  }
}

void init_controllers_and_audio() {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG, "1");
  Uint32 SDL_INIT_FLAGS = SDL_INIT_GAMECONTROLLER;
  if(ENABLE_AUDIO) {
    SDL_INIT_FLAGS |= SDL_INIT_AUDIO;
  }
#ifdef NEWTONIA_NET_RTC
  // The netplay lobby's clipboard signaling uses SDL's clipboard API, which
  // lives in the video subsystem (GLUT still owns the actual window).
  SDL_INIT_FLAGS |= SDL_INIT_VIDEO;
#endif
  if(SDL_Init(SDL_INIT_FLAGS) == 0) {
    if( ENABLE_AUDIO && Mix_OpenAudio( 44100, MIX_DEFAULT_FORMAT, 2, 1024 ) < 0) {
      std::cout << "Unable to open audio device" << std::endl;
      std::cout << Mix_GetError() << std::endl;
    }
    // 64 channels: gen-20+ firefights (enemy shot cues, booms, boost and
    // missile loops) can pin 32 and silently drop new sounds. Channels 0/1
    // are reserved out of -1 allocation as a guaranteed-loop-free fallback
    // for must-hear booms (see play_priority_chunk in glgame.cpp).
    if(ENABLE_AUDIO) { Mix_AllocateChannels(64); Mix_ReserveChannels(2); }
    SDL_JoystickEventState(SDL_ENABLE);
    int opened = 0;
    for (int i = 0; i < SDL_NumJoysticks() && opened < 2; ++i) {
      if (SDL_IsGameController(i)) {
        controllers[opened] = SDL_GameControllerOpen(i);
        if (controllers[opened]) {
          controller_ids[opened] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[opened]));
          std::cout << "Controller " << opened+1 << ": " << SDL_GameControllerName(controllers[opened]) << std::endl;
          opened++;
        } else {
          std::cout << "Could not open gamecontroller " << i << ": " << SDL_GetError() << std::endl;
        }
      }
    }
    if(opened == 0) std::cout << "No controllers found" << std::endl;
  } else {
    std::cout << "SDL Failed to initialize" << std::endl;
    std::cout << SDL_GetError() << std::endl;
  }
}

void init(int &argc, char* argv[], float width, float height);

int main(int argc, char* argv[]) {
  srand(time(NULL));
#ifdef NEWTONIA_NET_RTC
  // Hidden CI/debug hook (same as xbox_main.cpp): run the netplay loopback
  // self-test and exit. Works headless — no window or GL is created.
  {
    const char *st = SDL_getenv("NEWTONIA_NET_SELFTEST");
    if (st && st[0] == '1' && st[1] == '\0') {
      std::cout << "NEWTONIA_NET_SELFTEST: running loopback self-test..." << std::endl;
      bool ok = net_selftest();
      std::cout << (ok ? "NET SELFTEST PASS" : "NET SELFTEST FAIL") << std::endl;
      return ok ? 0 : 1;
    }
    // Same idea for the M2 room-code relay (needs a live signal server:
    // wrangler dev locally, or NEWTONIA_SIGNAL_URL / the preferences INI's
    // signal_url to point elsewhere).
    const char *ss = SDL_getenv("NEWTONIA_SIGNAL_SELFTEST");
    if (ss && ss[0] == '1' && ss[1] == '\0') {
      load_preferences();  // net_signal_url() honours the INI override
      std::cout << "NEWTONIA_SIGNAL_SELFTEST: running relay self-test..." << std::endl;
      bool ok = net_signal_selftest();
      std::cout << (ok ? "SIGNAL SELFTEST PASS" : "SIGNAL SELFTEST FAIL") << std::endl;
      return ok ? 0 : 1;
    }
  }
#endif
  if (!steam_init())
    std::cout << "Steam API unavailable (offline / direct-launch mode)" << std::endl;
  // Must precede the first frame: the Steam backend registers its stat
  // callbacks here, and the SDK's automatic stats delivery is dispatched on
  // an early SteamAPI_RunCallbacks() — unheard registrations queue forever.
  Achievements::init();
  // Register the invite backend before the first callback pump (an invite
  // accepted while running arrives via a Steam callback), and capture a
  // "+connect <code>" the platform may have appended on a cold launch — the
  // menu drains it and joins the room.
  Invites::init();
  Invites::capture_launch(argc, argv);
  load_preferences();
  old_width  = g_prefs.window_width;
  old_height = g_prefs.window_height;
  init(argc, argv, g_prefs.window_width, g_prefs.window_height);
#ifdef __APPLE__
  enable_game_mode_macos();
#endif
  if (g_prefs.fullscreen) {
    is_fullscreen = true;
#ifdef __APPLE__
    // Defer the native fullscreen transition until the window is on screen
    // (handled in draw()); toggleFullScreen: is unreliable before launch
    // finishes.  The cursor is hidden once that transition completes.
    s_needs_fullscreen = true;
#else
    glutFullScreen();
    set_cursor_hidden(true);
#endif
  }
  init_controllers_and_audio();
  atexit([]{ save_preferences(); if (game) game->focus_lost(); Presence::clear(); Invites::clear_joinable(); steam_shutdown(); });
  game = new StateManager();
  for(int i = 0; i < 2; i++) {
    if(controllers[i]) game->controller_added(controllers[i]);
  }
#ifdef __APPLE__
  install_macos_focus_observer(on_focus_lost, on_focus_gained);
#endif
  resize(glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
  glutMainLoop();
  save_preferences();
  for(int i = 0; i < 2; i++) {
    if(controllers[i] && SDL_GameControllerGetAttached(controllers[i])) {
      SDL_GameControllerClose(controllers[i]);
    }
  }
  StateManager *g = game;
  game = nullptr;
  delete g;
  Asteroid::free_sounds();
  Typer::cleanup();
  gles2_shutdown();
  return EXIT_SUCCESS;
}

void init(int &argc, char* argv[], float width, float height) {
  glutInit(&argc, argv);

  // Request an OpenGL 3.3 Core Profile context.
  // Legacy immediate-mode and display-list functions are not available in
  // Core Profile; all rendering goes through gles2_compat (VBO/VAO/GLSL).
#ifdef __APPLE__
  // Apple's GLUT uses a dedicated flag.  The driver promotes this to the
  // highest supported Core Profile version (up to 4.1 on macOS).
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_3_2_CORE_PROFILE);
#else
  glutInitContextVersion(3, 3);
  glutInitContextProfile(GLUT_CORE_PROFILE);
  glutInitContextFlags(GLUT_FORWARD_COMPATIBLE);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
#endif

  glutInitWindowSize(width, height);
  glutCreateWindow("Newtonia");

#ifdef __APPLE__
  // VSync: block glutSwapBuffers() at the display's vertical blank.
  {
    CGLContextObj ctx = CGLGetCurrentContext();
    GLint swapInterval = 1;
    CGLSetParameter(ctx, kCGLCPSwapInterval, &swapInterval);
  }
#endif

  // Initialise the VBO/VAO/GLSL shim (compiles shaders, creates GPU buffers).
  gles2_init();

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glutDisplayFunc(draw);
  // Deliver only real presses: without this, holding a key streams
  // auto-repeat keydowns that re-arm the semi-automatic primaries
  // (Beam/Lance/Shock disarm after each bolt) every repeat — a held fire
  // key kept firing despite the one-bolt-per-pull design. The game tracks
  // held state itself via keyboard_up, so repeats carry no information.
  glutIgnoreKeyRepeat(1);
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutSpecialFunc(special);
  glutSpecialUpFunc(special_up);
  glutReshapeFunc(resize);
#ifdef __APPLE__
  glutPassiveMotionFunc(mouse_passive);
#endif
  glutVisibilityFunc(isVisible);
}

#endif // !__ANDROID__ && !__EMSCRIPTEN__
