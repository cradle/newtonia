/* glut.cpp - GLUT Abstraction layer for Game */
#include <stdlib.h> // For EXIT_SUCCESS

#include <SDL.h>
#include <SDL_mixer.h>

#include "state_manager.h"

#ifdef __APPLE__
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
bool BLUR = ALLOW_BLUR;
double blur_factor = 0.5;
SDL_GameController *controller = NULL;

void draw() {
  game->draw();
  if(BLUR) {
    glAccum(GL_MULT, blur_factor);
    glAccum(GL_ACCUM, 1.0-blur_factor);
    glAccum(GL_RETURN, 1.0);
  }
  glutSwapBuffers();
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
    } else {
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
  game->tick(current_time - last_tick_time);
  last_tick_time = current_time;
  check_controller();
  glutPostRedisplay();
}

void isVisible(int state) {
  if(state == GLUT_VISIBLE) {
    last_tick_time = glutGet(GLUT_ELAPSED_TIME);
    glutVisibilityFunc(NULL);
    glutIdleFunc(tick);
  }
}

void init_controllers() {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG, "1");
  SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO); // custom mappings SDL_HINT_GAMECONTROLLERCONFIG
  if( Mix_OpenAudio( 44100, MIX_DEFAULT_FORMAT, 2, 1024 ) < 0) {
    std::cout << "Unable to open audio device" << std::endl;
  }
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
}

void init(int &argc, char* argv[], float width, float height);

int main(int argc, char* argv[]) {
  init(argc, argv, 800, 600);
  init_controllers();
  game = new StateManager();
  glutMainLoop();
  if (SDL_GameControllerGetAttached(controller)) {
    SDL_GameControllerClose(controller);
  }
  delete game;
  return EXIT_SUCCESS;
}

void init(int &argc, char* argv[], float width, float height) {
  glutInit(&argc, argv);
  int DISPLAY_TYPE = GLUT_RGBA;
  if(ALLOW_BLUR) {
    DISPLAY_TYPE = DISPLAY_TYPE | GLUT_ACCUM;
  }
  glutInitDisplayMode(DISPLAY_TYPE);
  glutInitWindowSize(width, height);
  glutCreateWindow("Newtonia");

  //glEnable(GL_DEPTH_TEST);
  //glDepthFunc(GL_LESS);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_POINT_SMOOTH);
  glEnable(GL_ACCUM);
  glClear(GL_ACCUM_BUFFER_BIT);

  glutDisplayFunc(draw);
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutSpecialFunc(special);
  glutSpecialUpFunc(special_up);
  glutReshapeFunc(resize);
  glutVisibilityFunc(isVisible);
}
