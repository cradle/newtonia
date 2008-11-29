 #include <stdlib.h> // For EXIT_SUCCESS

#include "glgame.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <iostream>

GLGame* glgame;

void draw() {
  glgame->draw();
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
  glgame->keyboard(key, x, y);
}

void keyboard_up(unsigned char key, int x, int y) {
  glgame->keyboard_up(key, x, y);
}

void resize(int width, int height) {
  glgame->resize(width, height);
}

void tick() {
  glgame->tick();
}

void isVisible(int state) {
  if(state == GLUT_VISIBLE) {
    glutVisibilityFunc(NULL);
    glutIdleFunc(tick);
  }
}

int main(int argc, char** argv) {
  glgame = new GLGame(15000,15000);
  glgame->init(argc, argv, 800, 600);

  glutSetKeyRepeat(GLUT_KEY_REPEAT_OFF);
  glutDisplayFunc(draw);
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutReshapeFunc(resize);
  glutVisibilityFunc(isVisible);
  //TODO: stop using glut, use SDL

  glgame->run();
  std::cout << "deleting" << std::endl;
  delete glgame;

  return EXIT_SUCCESS;
}
