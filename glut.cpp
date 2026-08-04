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
#include "shot_scene.h"
#include "state.h"
#include "net_transport.h"
#include "net_signal.h"
#include "net_identity.h"
#include "achievements.h"
#include "presence.h"
#include "invites.h"
#include "world_sound.h"

// gl_compat.h pulls in GLUT (for window management) and gles2_compat.h
// (for the VBO/VAO/shader shim that replaces all legacy GL calls).
#include "gl_compat.h"
#include "mat4.h"
#include "replay.h"

#include <cstdio>
#include <string>

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
// XInput2 for the direct touchscreen listener (Steam Deck) — see
// touch_listener_init below.
#include <X11/extensions/XInput2.h>
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

static void draw_tap_debug();

void draw() {
  if (!game) return;
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  last_render_time = current_time;
  game->draw();  // StateManager::draw zeroes the dbg counters at entry
  s_last_frame_draws = g_gles2_dbg_draws;
  s_last_frame_segs  = g_gles2_dbg_line_segs;
  draw_tap_debug();
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

// Steam Deck touch on menus: the Deck's touchscreen (and any desktop
// touchscreen) reaches a GLUT window as synthesized pointer clicks —
// gamescope in Gaming Mode and the desktop compositors both emulate a
// left-button press/release at the touch point. Forward releases as taps,
// matching the mobile ports where menu selections fire on finger-up; this
// doubles as plain mouse support on the menus. In-game clicks stay inert
// (GLGame::touch_tap guards on is_touch_mode()).
//
// NEWTONIA_TAP_DEBUG=1 (Steam launch options: NEWTONIA_TAP_DEBUG=1
// %command%) overlays the last input event on screen — field diagnosis for
// whether clicks/touches reach the game at all, and where, without needing
// a terminal. The overlay is persistent while enabled (it starts as a
// status line the moment the env var is set), so "no overlay at all"
// always means the env var isn't set, never "no events yet". Every event
// is also logged to stdout (greppable in headless driver runs and
// Desktop-Mode terminal launches).
static bool s_tap_debug = false;
static std::string s_tap_debug_line;

static void tap_debug_note(const char *line) {
  std::cout << "tap: " << line << std::endl;
  if (s_tap_debug) s_tap_debug_line = line;
}

// The one tap delivery point for both input paths (mouse release, XI2
// touch end). The mouse path and the touch listener can BOTH see the same
// physical tap in environments that emulate clicks from touch, so a second
// tap right on top of the previous one (time and place) is dropped.
static void forward_tap(float px, float py) {
  static int last_ms = -100000;
  static float last_px = -1000, last_py = -1000;
  int now = glutGet(GLUT_ELAPSED_TIME);
  float dx = px - last_px, dy = py - last_py;
  bool dup = (now - last_ms) < 250 && dx * dx + dy * dy < 30.0f * 30.0f;
  last_ms = now; last_px = px; last_py = py;
  if (dup || !game) return;
  int w = glutGet(GLUT_WINDOW_WIDTH), h = glutGet(GLUT_WINDOW_HEIGHT);
  if (w <= 0 || h <= 0) return;
  game->touch_tap(px / (float)w, py / (float)h);
}

void mouse(int button, int state, int x, int y) {
  char buf[96];
  snprintf(buf, sizeof(buf), "MOUSE B%d %s %d,%d", button,
           state == GLUT_DOWN ? "DOWN" : "UP", x, y);
  tap_debug_note(buf);
  if (button != GLUT_LEFT_BUTTON || state != GLUT_UP) return;
  forward_tap((float)x, (float)y);
}

// Drawn from draw() after the state renders, ortho like the menus'.
static void draw_tap_debug() {
  if (!s_tap_debug || s_tap_debug_line.empty()) return;
  int w = glutGet(GLUT_WINDOW_WIDTH), h = glutGet(GLUT_WINDOW_HEIGHT);
  if (w <= 0 || h <= 0) return;
  float ortho[16];
  mat4_ortho(ortho, (float)-w, (float)w, (float)-h, (float)h, -1.0f, 1.0f);
  gles2_set_vp(ortho);
  // Typer coordinates are virtual units (multiplied by Typer::scale), so
  // convert the ortho half-height into Typer units, as Intro::draw does.
  float top = h / Typer::scale;
  Typer::draw_centered(0, -top * 0.8f, s_tap_debug_line.c_str(), 14);
}

#ifdef __linux__
// Steam Deck touch, the real delivery path. Both gamescope (Gaming Mode)
// and the Plasma desktop present the touchscreen to X11 clients as
// XInput2 TOUCH events; the pointer-emulated clicks we first relied on
// never reached the freeglut window in either environment (field result
// 2026-07-25 on the beta depot — mouse clicks work, touches arrive as
// nothing). freeglut 2.8 (the Steam runtime's libglut) selects XI2
// pointer events but no touch masks, and its event loop drops XI2 event
// types it doesn't know, so touch can't be handled through it. Instead:
// a SECOND X connection announces XI 2.2 and selects touch on the GLUT
// window (a per-client selection — freeglut's own connection is
// unaffected), polled non-blocking each tick; a touch sequence's END
// forwards the same tap a mouse release does. Failure at any init step
// logs and degrades to mouse-only, exactly the pre-listener behavior.
static Display *s_touch_dpy = NULL;
static int s_touch_opcode = -1;
static int s_touch_active_id = -1;  // first-finger tracking: extra fingers
                                    // during a sequence don't fire taps

// X errors on either connection are fatal by default (Xlib exits the
// process); async errors from an XI2 selection would kill the game long
// after the offending call. Log-and-continue instead.
static int x_error_logger(Display *dpy, XErrorEvent *e) {
  char text[128];
  XGetErrorText(dpy, e->error_code, text, sizeof(text));
  char buf[192];
  snprintf(buf, sizeof(buf), "X ERROR %s req %d.%d", text,
           e->request_code, e->minor_code);
  tap_debug_note(buf);
  return 0;
}

static void touch_listener_init() {
  Display *glut_dpy = glXGetCurrentDisplay();
  Window win = glut_dpy ? (Window)glXGetCurrentDrawable() : 0;
  if (!win) { tap_debug_note("TOUCH LISTENER OFF - no GLX window"); return; }
  s_touch_dpy = XOpenDisplay(DisplayString(glut_dpy));
  if (!s_touch_dpy) { tap_debug_note("TOUCH LISTENER OFF - XOpenDisplay failed"); return; }
  XSetErrorHandler(x_error_logger);
  int event, error;
  int major = 2, minor = 2;  // must announce XI 2.2+ or touch is withheld
  if (!XQueryExtension(s_touch_dpy, "XInputExtension", &s_touch_opcode,
                       &event, &error) ||
      XIQueryVersion(s_touch_dpy, &major, &minor) != Success ||
      (major * 100 + minor) < 202) {
    tap_debug_note("TOUCH LISTENER OFF - no XInput 2.2");
    XCloseDisplay(s_touch_dpy);
    s_touch_dpy = NULL;
    return;
  }
  // XIAllDevices, not XIAllMasterDevices: a touchscreen that floats as an
  // unattached slave (input remapping setups do this) only matches an
  // all-devices selection; attached ones match either way. Duplicate
  // master+slave copies of one touch collapse in forward_tap's dedup.
  XIEventMask mask;
  unsigned char flags[XIMaskLen(XI_LASTEVENT)] = {0};
  mask.deviceid = XIAllDevices;
  mask.mask_len = sizeof(flags);
  mask.mask = flags;
  XISetMask(flags, XI_TouchBegin);
  XISetMask(flags, XI_TouchUpdate);
  XISetMask(flags, XI_TouchEnd);
  XISelectEvents(s_touch_dpy, win, &mask, 1);
  // Diagnostic spy (log-only, never forwards a tap): RAW touch/button on
  // the root window. Raw selections are non-exclusive and delivered no
  // matter which window the event routes to, so with NEWTONIA_TAP_DEBUG
  // the log answers the one question a silent window can't: does the X
  // server see the finger AT ALL, or is the touchscreen consumed upstream
  // (Steam Input) before X ever hears about it?
  if (s_tap_debug) {
    XIEventMask raw_mask;
    unsigned char raw_flags[XIMaskLen(XI_LASTEVENT)] = {0};
    raw_mask.deviceid = XIAllDevices;
    raw_mask.mask_len = sizeof(raw_flags);
    raw_mask.mask = raw_flags;
    XISetMask(raw_flags, XI_RawTouchBegin);
    XISetMask(raw_flags, XI_RawTouchEnd);
    XISetMask(raw_flags, XI_RawButtonPress);
    XISetMask(raw_flags, XI_RawButtonRelease);
    XISelectEvents(s_touch_dpy, DefaultRootWindow(s_touch_dpy), &raw_mask, 1);
  }
  XFlush(s_touch_dpy);
  tap_debug_note(s_tap_debug ? "TAP DEBUG ON - TOUCH LISTENER OK"
                             : "TOUCH LISTENER OK");
}

static void touch_listener_poll() {
  if (!s_touch_dpy) return;
  while (XPending(s_touch_dpy)) {
    XEvent ev;
    XNextEvent(s_touch_dpy, &ev);
    if (ev.type != GenericEvent || ev.xcookie.extension != s_touch_opcode)
      continue;
    if (!XGetEventData(s_touch_dpy, &ev.xcookie)) continue;
    char buf[96];
    switch (ev.xcookie.evtype) {
      case XI_TouchBegin: {
        XIDeviceEvent *de = (XIDeviceEvent *)ev.xcookie.data;
        snprintf(buf, sizeof(buf), "TOUCH BEGIN %d,%d dev %d/%d",
                 (int)de->event_x, (int)de->event_y, de->deviceid,
                 de->sourceid);
        tap_debug_note(buf);
        if (s_touch_active_id < 0) s_touch_active_id = de->detail;
        break;
      }
      case XI_TouchEnd: {
        XIDeviceEvent *de = (XIDeviceEvent *)ev.xcookie.data;
        snprintf(buf, sizeof(buf), "TOUCH END %d,%d dev %d/%d",
                 (int)de->event_x, (int)de->event_y, de->deviceid,
                 de->sourceid);
        tap_debug_note(buf);
        if (de->detail == s_touch_active_id) {
          s_touch_active_id = -1;
          forward_tap((float)de->event_x, (float)de->event_y);
        }
        break;
      }
      case XI_RawTouchBegin:
      case XI_RawTouchEnd:
      case XI_RawButtonPress:
      case XI_RawButtonRelease: {
        // Spy only — proves the server saw the input; never taps.
        XIRawEvent *re = (XIRawEvent *)ev.xcookie.data;
        const char *kind =
            ev.xcookie.evtype == XI_RawTouchBegin     ? "RAW TOUCH DOWN"
            : ev.xcookie.evtype == XI_RawTouchEnd     ? "RAW TOUCH UP"
            : ev.xcookie.evtype == XI_RawButtonPress  ? "RAW BTN DOWN"
                                                      : "RAW BTN UP";
        snprintf(buf, sizeof(buf), "%s dev %d/%d detail %d", kind,
                 re->deviceid, re->sourceid, re->detail);
        tap_debug_note(buf);
        break;
      }
      default:
        break;
    }
    XFreeEventData(s_touch_dpy, &ev.xcookie);
  }
}
#endif // __linux__

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
  touch_listener_poll();
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
    // for must-hear booms (see play_priority_chunk in glgame.cpp), plus
    // WorldSound's per-channel-volume pool behind them (world_sound.h).
    if(ENABLE_AUDIO) {
      Mix_AllocateChannels(64);
      Mix_ReserveChannels(WorldSound::FIRST_CHANNEL + WorldSound::POOL);
    }
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

void init(int &argc, char* argv[], float width, float height,
          bool shot_mode = false);

// ---- screenshot harness (NEWTONIA_SHOT; see shots/README.md) ----
// A parallel, minimal frame loop: no StateManager, no Steam, no preference
// writes, no saves. ShotScene builds the state; this loop ticks it on a
// FIXED 16 ms step (deterministic given the scene's seed, and faster than
// real time when the machine allows), draws, captures, and leaves.
static State *shot_state = nullptr;
static int shot_sim_done = 0;
static bool shot_ok = false;
static bool shot_written = false;

static void shot_reshape(int w, int h) {
  Typer::resize(w, h);
  if (shot_state) shot_state->resize(w, h);
}

static void shot_display() {
  if (!shot_state) return;
  shot_state->draw();
  int w = glutGet(GLUT_WINDOW_WIDTH), h = glutGet(GLUT_WINDOW_HEIGHT);
  ShotScene::draw_overlays(w, h);
  if (shot_sim_done >= ShotScene::sim_ms() && !shot_written) {
    shot_written = true;
    // Read the back buffer BEFORE the swap — its contents are undefined
    // after.
    shot_ok = ShotScene::capture(w, h);
    glutSwapBuffers();
    glutLeaveMainLoop();
    return;
  }
  glutSwapBuffers();
}

static void shot_tick() {
  if (!shot_state) return;
  if (shot_sim_done < ShotScene::sim_ms()) {
    ShotScene::pump_keys(shot_state, shot_sim_done);
    const int STEP = 16;
    shot_state->tick(STEP);
    shot_sim_done += STEP;
  }
  glutPostRedisplay();
}

static int shot_main(int argc, char *argv[]) {
  if (!ShotScene::init()) return 1;
  load_preferences();  // star density etc.; never written back
  int w = ShotScene::width()  > 0 ? ShotScene::width()  : g_prefs.window_width;
  int h = ShotScene::height() > 0 ? ShotScene::height() : g_prefs.window_height;
  init(argc, argv, (float)w, (float)h, /*shot_mode=*/true);
  init_controllers_and_audio();  // sound assets load; SDL_AUDIODRIVER=dummy
                                 // is the headless escape (TESTING.md)
  shot_state = ShotScene::build_state();
  if (!shot_state) return 1;
  shot_reshape(glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
  glutMainLoop();
  return shot_ok ? EXIT_SUCCESS : 1;
}

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
  // Same hidden-hook shape, but NOT under NEWTONIA_NET_RTC: replays are a
  // solo feature and the netless binary records too. Deterministic check of
  // the recorder's keyframe-ordering invariant — the one the online host's
  // first 100 ms and every client rejoin depend on, and the one the e2e
  // drivers structurally cannot provoke (see replay_selftest.cpp).
  {
    const char *rs = SDL_getenv("NEWTONIA_REPLAY_SELFTEST");
    if (rs && rs[0] == '1' && rs[1] == '\0') {
      std::cout << "NEWTONIA_REPLAY_SELFTEST: running recorder self-test..."
                << std::endl;
      bool ok = Replay::selftest();
      std::cout << (ok ? "REPLAY SELFTEST PASS" : "REPLAY SELFTEST FAIL")
                << std::endl;
      return ok ? 0 : 1;
    }
  }
  // Screenshot harness: render one composed scene to an image and exit —
  // before Steam/achievements/invites init, none of which a shot needs.
  if (ShotScene::requested()) return shot_main(argc, argv);
  s_tap_debug = SDL_getenv("NEWTONIA_TAP_DEBUG") != NULL;
  if (s_tap_debug) tap_debug_note("TAP DEBUG ON");
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
  // Warm the netplay verification credential (NETPLAY.md V1): minting a Steam
  // Web-API ticket is async, so kick it off at startup — it completes during
  // menu navigation and is ready before the first host/join, closing the race
  // where a host announced its identity before the ticket existed and stayed
  // unverified for the session. A no-op off Steam (returns "").
  (void)net_local_verify_credential();
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

void init(int &argc, char* argv[], float width, float height,
          bool shot_mode) {
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

  if (shot_mode) {
    // Screenshot harness: its own display/idle/reshape trio, and nothing
    // else — the interactive callbacks below dereference the StateManager
    // (never created in shot mode) and write preferences on reshape.
    glutDisplayFunc(shot_display);
    glutReshapeFunc(shot_reshape);
    glutIdleFunc(shot_tick);
    return;
  }
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
  glutMouseFunc(mouse);
  glutReshapeFunc(resize);
#ifdef __linux__
  // After glutCreateWindow: the GLX context is current (gles2_init above
  // relies on that too), so the window/display are retrievable.
  touch_listener_init();
#endif
#ifdef __APPLE__
  glutPassiveMotionFunc(mouse_passive);
#endif
  glutVisibilityFunc(isVisible);
}

#endif // !__ANDROID__ && !__EMSCRIPTEN__
