#include "glgame.h"
#include "glship.h"
#include "glcar.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

GLGame::GLGame(float width, float height) {
  GLShip* ship = new GLShip(-width*3/4,-height*3/4);
  ship->set_keys('a','d','w',' ');
  ships.push_back(ship);

  //TODO: Make test for this type of overloading
  ship = new GLCar(width*3/4,height*3/4);
  ship->set_keys('j','l','i','/');
  ships.push_back(ship);

  resize_ships(width, height);

  window_width = width;
  window_height = height;
}

void GLGame::tick(void) {
  int current_time = glutGet(GLUT_ELAPSED_TIME);

  std::vector<GLShip*>::iterator ship;
  for(ship = ships.begin(); ship != ships.end(); ship++) {
    //TODO: find out how to use vectors better
    (*ship)->step(current_time - last_tick);
  }

  last_tick = current_time;
  glutPostRedisplay();

  //Fix: Don't do this
  //TODO: fix this
  GLShip::collide(ships[0], ships[1]);
}

void GLGame::resize_ships(int width, int height) {
  std::vector<GLShip*>::iterator ship;
  for(ship = ships.begin(); ship != ships.end(); ship++) {
    (*ship)->resize(width, height);
  }
}

void GLGame::resize(int width, int height) {
  window_width = width;
  window_height = height;

  resize_ships(width, height);

  glViewport(0, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  //TODO: Refactor into 0, width, 0, height, and change code elsewhere
  glOrtho(-width/2, width/2, -height/2, height/2, -1, 1);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void GLGame::draw(void) {
  glClear(GL_COLOR_BUFFER_BIT);
  glLoadIdentity();
  std::vector<GLShip*>::iterator ship;
  for(ship = ships.begin(); ship != ships.end(); ship++) {
    (*ship)->draw();
  }
  glutSwapBuffers();
}

#define ITER(collection, element) for(element = collection.begin(); element != collection.end(); element++)

void GLGame::keyboard (unsigned char key, int x, int y) {
  std::vector<GLShip*>::iterator ship;
  for(ship = ships.begin(); ship != ships.end(); ship++) {
    (*ship)->input(key);
  }
}
void GLGame::keyboard_up (unsigned char key, int x, int y) {
  std::vector<GLShip*>::iterator ship;
  for(ship = ships.begin(); ship != ships.end(); ship++) {
    (*ship)->input(key, false);
  }
}

void GLGame::init(int argc, char** argv) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
  glutInitWindowSize(window_width, window_height);
  glutCreateWindow("Asteroids");

  glShadeModel(GL_FLAT);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

void GLGame::run(void) {
  last_tick = glutGet(GLUT_ELAPSED_TIME);
  glutMainLoop();
}
