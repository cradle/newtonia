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
  if (key == 27) // ESC
    exit(0);
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
  glgame = new GLGame(10000,10000);
  glgame->init(argc, argv, 800, 600);

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
