// Desktop-only entry point. Android uses android_main.cpp; web uses web_main.cpp.
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

#include <stdlib.h> // For EXIT_SUCCESS

#include <SDL.h>
#include <SDL_mixer.h>

#include "state_manager.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#include <OpenGL/OpenGL.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#include <GL/freeglut_std.h>
#include <GL/freeglut_ext.h>
#endif

// Glut callbacks cannot be member functions. Need to pre-declare game object
StateManager *game;

bool ALLOW_BLUR = false;
double blur_factor = 0.15;
SDL_GameController *controllers[2] = {NULL, NULL};
SDL_JoystickID controller_ids[2] = {-1, -1};
bool ENABLE_AUDIO = true;

int last_render_time;
void draw() {
  if (!game) return;
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  //cout << "fps: " << 1000.0 / (current_time - last_render_time) << endl;
  last_render_time = current_time;
  game->draw();
  if(ALLOW_BLUR) {
    glAccum(GL_MULT, blur_factor);
    glAccum(GL_ACCUM, 1.0-blur_factor);
    glAccum(GL_RETURN, 1.0);
  }
  glutSwapBuffers();
  //glutPostRedisplay();
  //glFlush();
}

int old_x = 50;
int old_y = 50;
int old_width = 320;
int old_height = 320;

void keyboard(unsigned char key, int x, int y) {
  switch (key) {
  case 'B':
    blur_factor = (1+blur_factor) / 2.0;
    cout << blur_factor << endl;
    break;
  case 'b':
    blur_factor = blur_factor / 2.0;
    cout << blur_factor << endl;
    break;
  case '\r':
    if(glutGetModifiers() != GLUT_ACTIVE_ALT) {
      break;
    }
  case 'f':
    // http://www.xmission.com/~nate/sgi/sgi-macosx.zip
    if (glutGet(GLUT_WINDOW_WIDTH) < glutGet(GLUT_SCREEN_WIDTH)) {
      old_x = glutGet(GLUT_WINDOW_X);
      old_y = glutGet(GLUT_WINDOW_Y);
      old_width = glutGet(GLUT_WINDOW_WIDTH);
      old_height = glutGet(GLUT_WINDOW_HEIGHT);
      glutFullScreen();
      glutSetCursor(GLUT_CURSOR_NONE);
    } else {
      glutPositionWindow(old_x, old_y);
      glutReshapeWindow(old_width, old_height);
      glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
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
}

void mouse_move(int x, int y) {
  game->mouse_move(x, y);
  glutWarpPointer(glutGet(GLUT_WINDOW_WIDTH)/2.0f, glutGet(GLUT_WINDOW_HEIGHT)/2.0f);
}

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

int last_tick_time;
void tick() {
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  //cout << "tps: " << 1000.0 / (current_time - last_tick_time) << endl;
  game->tick(current_time - last_tick_time);
  last_tick_time = current_time;
  check_controller();
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
  // custom mappings SDL_HINT_GAMECONTROLLERCONFIG;
  if(ENABLE_AUDIO) {
    SDL_INIT_FLAGS |= SDL_INIT_AUDIO; // this does not seem to work :S
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
  init(argc, argv, 800, 600);
  init_controllers_and_audio();
  game = new StateManager();
  for(int i = 0; i < 2; i++) {
    if(controllers[i]) game->controller_added(controllers[i]);
  }
  resize(glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
  glutMainLoop();
  for(int i = 0; i < 2; i++) {
    if(controllers[i] && SDL_GameControllerGetAttached(controllers[i])) {
      SDL_GameControllerClose(controllers[i]);
    }
  }
  delete game;
  return EXIT_SUCCESS;
}

void init(int &argc, char* argv[], float width, float height) {
  glutInit(&argc, argv);
  int DISPLAY_TYPE = GLUT_RGBA | GLUT_DOUBLE;
  if(ALLOW_BLUR) {
    DISPLAY_TYPE = DISPLAY_TYPE | GLUT_ACCUM;
  }
  glutInitDisplayMode(DISPLAY_TYPE);
  glutInitWindowSize(width, height);
  glutCreateWindow("Newtonia");

#ifdef __APPLE__
  // Enable VSync so glutSwapBuffers() blocks until the display's vertical
  // blank. Without this, the idle loop runs uncapped and produces jank on
  // external monitors (e.g. a 50 Hz display on M1) because rendered frames
  // are not aligned to the screen's refresh cycle.
  {
    CGLContextObj ctx = CGLGetCurrentContext();
    GLint swapInterval = 1;
    CGLSetParameter(ctx, kCGLCPSwapInterval, &swapInterval);
  }
#endif

  //glEnable(GL_DEPTH_TEST);
  //glDepthFunc(GL_LESS);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_POINT_SMOOTH);
  if(ALLOW_BLUR) {
    glEnable(GL_ACCUM);
    glClear(GL_ACCUM_BUFFER_BIT);
  }

  glutDisplayFunc(draw);
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutSpecialFunc(special);
  glutSpecialUpFunc(special_up);
  glutReshapeFunc(resize);
  glutVisibilityFunc(isVisible);
}

#endif // !__ANDROID__ && !__EMSCRIPTEN__
