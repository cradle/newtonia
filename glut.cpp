#include <stdlib.h> // For EXIT_SUCCESS

#include <SDL.h>
#include <SDL_mixer.h>

#include "state_manager.h"

#ifdef __APPLE__
#define glutLeaveMainLoop() exit(EXIT_SUCCESS)
#include <GLUT/glut.h>
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
SDL_GameController *controller = NULL;
bool ENABLE_AUDIO = true;
int primaryWindow;
int secondaryWindow;

int last_render_time;
void draw(int window, int window_index) {
  glutSetWindow(window);
  game->draw(window_index);
  if(ALLOW_BLUR) {
    glAccum(GL_MULT, blur_factor);
    glAccum(GL_ACCUM, 1.0-blur_factor);
    glAccum(GL_RETURN, 1.0);
  }
  glutSwapBuffers();
  glutPostRedisplay();
  glFlush();
}

int num_windows = 0;

void drawBoth() {
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  //cout << "fps: " << 1000.0 / (current_time - last_render_time) << endl;
  last_render_time = current_time;
  draw(primaryWindow, 0);
  if(num_windows == 2) {
    draw(secondaryWindow, 1);
  }
}

int old_x = 50;
int old_y = 50;
int old_width = 320;
int old_height = 320;

int initWindow(); // forward declare

void keyboard(unsigned char key, int x, int y) {
  switch (key) {
  case '1':
    if(num_windows == 2) {
      glutDestroyWindow(secondaryWindow);
      glutSetWindow(primaryWindow);
      num_windows = 1;
    }
    break;
  case '2':
    if(num_windows == 1) {
      secondaryWindow = initWindow();
    }
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
    glutFullScreenToggle();
    //if (glutGet(GLUT_WINDOW_WIDTH) < glutGet(GLUT_SCREEN_WIDTH)) {
    //  glutFullScreen();
    //} else {
    //  glutLeaveFullScreen();
    //}
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
  game->resize(width, height);
}

void mouse_move(int x, int y) {
  game->mouse_move(x, y);
  glutWarpPointer(glutGet(GLUT_WINDOW_WIDTH)/2.0f, glutGet(GLUT_WINDOW_HEIGHT)/2.0f);
}

void check_controller() {
  SDL_Event e;
  while(SDL_PollEvent(&e)) {
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
    //TODO: SDL_JOYDEVICEADDED or SDL_JOYDEVICEREMOVED
    //SDL_GameControllerEventState(int state);
    SDL_JoystickEventState(SDL_ENABLE);
    if(SDL_NumJoysticks() == 0) {
      std::cout << "No joysticks" << std::endl;
    } else {
      for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
          controller = SDL_GameControllerOpen(i);
          if (controller) {
            std::cout << "Controller: " << SDL_GameControllerName(controller) << std::endl;
            break;
          } else {
            std::cout <<  "Could not open gamecontroller " << i << ":" << SDL_GetError() << std::endl;
          }
        } else {
          std::cout << "Not controller" << std::endl;
        }
      }
    }
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
  glutMainLoop();
  if (SDL_GameControllerGetAttached(controller)) {
    SDL_GameControllerClose(controller);
  }
  delete game;
  return EXIT_SUCCESS;
}

int initWindow() {
  int window_index = glutCreateWindow("Newtonia");
  num_windows++;

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

  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutSpecialFunc(special);
  glutSpecialUpFunc(special_up);
  glutReshapeFunc(resize);
  glutDisplayFunc(drawBoth);

  return window_index;
}


void init(int &argc, char* argv[], float width, float height) {
  glutInit(&argc, argv);
  int DISPLAY_TYPE = GLUT_RGBA | GLUT_DOUBLE;
  if(ALLOW_BLUR) {
    DISPLAY_TYPE = DISPLAY_TYPE | GLUT_ACCUM;
  }
  glutInitDisplayMode(DISPLAY_TYPE);
  glutInitWindowSize(width, height);

  primaryWindow = initWindow();

  glutVisibilityFunc(isVisible);
}
