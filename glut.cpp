/* glut.cpp - GLUT Abstraction layer for Game */
#include <stdlib.h> // For EXIT_SUCCESS

#include "state_manager.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

// Glut callbacks cannot be member functions. Need to pre-declare game object
StateManager *game;

void draw() {
  game->draw();
  glutSwapBuffers();
}

int old_x = 50;
int old_y = 50;
int old_width = 320;
int old_height = 320;

void keyboard(unsigned char key, int x, int y) {
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

int last_tick_time;
void tick() {
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  game->tick(current_time - last_tick_time);
  last_tick_time = current_time;
  glutPostRedisplay();
}

void isVisible(int state) {
  if(state == GLUT_VISIBLE) {
    last_tick_time = glutGet(GLUT_ELAPSED_TIME);
    glutVisibilityFunc(NULL);
    glutIdleFunc(tick);
  }
}

void init(int &argc, char** argv, float width, float height) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
  glutInitWindowSize(width, height);
  glutCreateWindow("Newtonia");

  glPointSize(2.5f);
  glLineWidth(1.2f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_POINT_SMOOTH);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

  glutDisplayFunc(draw);
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutReshapeFunc(resize);
  glutVisibilityFunc(isVisible);
}

int main(int argc, char** argv) {
  init(argc, argv, 800, 600);
  game = new StateManager();
  glutMainLoop();
  delete game;
  return EXIT_SUCCESS;
}
