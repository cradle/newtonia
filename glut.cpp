// Desktop-only entry point. Android uses android_main.cpp; web uses web_main.cpp.
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

#include <stdlib.h> // For EXIT_SUCCESS
#include <time.h>   // For time()

#include <SDL.h>
#include <SDL_mixer.h>

#include "state_manager.h"
#include "asteroid.h"
#include "typer.h"

// gl_compat.h pulls in GLUT (for window management) and gles2_compat.h
// (for the VBO/VAO/shader shim that replaces all legacy GL calls).
#include "gl_compat.h"

#ifdef __APPLE__
// CGL is needed for VSync configuration only.
#include <OpenGL/OpenGL.h>
extern "C" void activate_app_macos();
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
void activate_app_timer(int);
#endif

void draw() {
  if (!game) return;
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  last_render_time = current_time;
  game->draw();
  glutSwapBuffers();
#ifdef __APPLE__
  // Activate after the first rendered frame so the window is on screen before
  // we request focus (a 0ms timer fires before the window is visible).
  // Also schedule a 500ms retry: the fullscreen transition animation may not
  // have completed by the first frame, causing an intermittent miss.
  // activate_app_macos() is a no-op once the app is already active.
  if (s_needs_activation) {
    s_needs_activation = false;
    activate_app_macos();
    glutTimerFunc(500, activate_app_timer, 0);
  }
#endif
}

int old_x = 50;
int old_y = 50;
int old_width = 1280;
int old_height = 720;
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
  switch (key) {
  case '\r':
    if(glutGetModifiers() != GLUT_ACTIVE_ALT) {
      break;
    }
  case 'f':
    if (!is_fullscreen) {
      old_x = glutGet(GLUT_WINDOW_X);
      old_y = glutGet(GLUT_WINDOW_Y);
      old_width = glutGet(GLUT_WINDOW_WIDTH);
      old_height = glutGet(GLUT_WINDOW_HEIGHT);
      glutFullScreen();
      is_fullscreen = true;
#ifdef __APPLE__
      glutTimerFunc(300, hide_cursor_after_fullscreen, 0);
#else
      set_cursor_hidden(true);
#endif
    } else {
      is_fullscreen = false;
      set_cursor_hidden(false);
      glutPositionWindow(old_x, old_y);
      glutReshapeWindow(old_width, old_height);
    }
    break;
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
  if(!(key == '\r' && glutGetModifiers() == GLUT_ACTIVE_ALT))
    game->keyboard_up(key, x, y);
}

void special_up(int key, int x, int y) {
  keyboard_up(key+128, x, y);
}

void resize(int width, int height) {
  Typer::resize(width, height);
  if (game) game->resize(width, height);
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
  activate_app_macos(); // No-op if already active.
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
  check_controller();
#ifdef __linux__
  check_linux_focus();
#endif
#ifdef _WIN32
  check_windows_focus();
#endif
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
  if(SDL_Init(SDL_INIT_FLAGS) == 0) {
    if( ENABLE_AUDIO && Mix_OpenAudio( 44100, MIX_DEFAULT_FORMAT, 2, 1024 ) < 0) {
      std::cout << "Unable to open audio device" << std::endl;
      std::cout << Mix_GetError() << std::endl;
    }
    if(ENABLE_AUDIO) Mix_AllocateChannels(32);
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
  init(argc, argv, 800, 600);
  glutFullScreen();
  is_fullscreen = true;
#ifdef __APPLE__
  glutTimerFunc(300, hide_cursor_after_fullscreen, 0);
#else
  set_cursor_hidden(true);
#endif
  init_controllers_and_audio();
  atexit([]{ if (game) game->focus_lost(); });
  game = new StateManager();
  for(int i = 0; i < 2; i++) {
    if(controllers[i]) game->controller_added(controllers[i]);
  }
#ifdef __APPLE__
  install_macos_focus_observer(on_focus_lost, on_focus_gained);
#endif
  resize(glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
  glutMainLoop();
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
