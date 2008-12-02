/* glut.cpp - GLUT Abstraction layer for Game */
#include <stdlib.h> // For EXIT_SUCCESS
#include "glgame.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

// Glut callbacks cannot be member functions. Need to pre-declare game object
GLGame* game;

void draw() {
  game->draw();
}

void keyboard(unsigned char key, int x, int y) {
  static int old_x = 50;
  static int old_y = 50;
  static int old_width = 320;
  static int old_height = 320;

  switch (key) {
  case 27:
    exit(0);
    break;
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

void keyboard_up(unsigned char key, int x, int y) {
  game->keyboard_up(key, x, y);
}

void resize(int width, int height) {
  game->resize(width, height);
}

void tick() {
  game->tick();
}

void isVisible(int state) {
  if(state == GLUT_VISIBLE) {
    glutVisibilityFunc(NULL);
    glutIdleFunc(tick);
  }
}

int main(int argc, char** argv) {
  game = new GLGame(15000,15000);
  game->init(argc, argv, 800, 600);

  glutDisplayFunc(draw);
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutReshapeFunc(resize);
  glutVisibilityFunc(isVisible);
  //TODO: stop using glut (use SDL?)
  game->run();
  delete game;
  return EXIT_SUCCESS;
}
