#include "glgame.h"
#include "glship.h"
#include "glcar.h"
#include "glstarfield.h"
#include "wrapped_point.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <iostream>
#include <vector>

GLGame::GLGame(float width, float height) : world(Point(width, height)) {
  GLShip* object = new GLShip(-width*3/4,-height*3/4);
  object->set_keys('a','d','w',' ');
  objects.push_back(object);

  object = new GLCar(width*3/4,height*3/4);
  object->set_keys('j','l','i','/');
  objects.push_back(object);
  
  WrappedPoint::set_boundaries(world);
  
  starfield = new GLStarfield(world);
}

GLGame::~GLGame() {
  //TODO: Make erase, use boost::ptr_vector? something better
  // std::erase(std::remove_if(v.begin(),v.end(),true), v.end());
  GLShip* object;
  while(!objects.empty()) {
    object = objects.back();
    objects.pop_back();
    delete object;
  }
}

void GLGame::tick(void) {
  int current_time = glutGet(GLUT_ELAPSED_TIME);

  //TODO: Learn function pointers or some loop abstraction
  std::vector<GLShip*>::iterator object;
  for(object = objects.begin(); object != objects.end(); object++) {
    //TODO: find out how to use vectors better
    (*object)->step(current_time - last_tick);
  }

  last_tick = current_time;
  glutPostRedisplay();

  //Fix: Don't do this
  //TODO: fix this, should iterate, then iterate inside it, yay n^2
  GLShip::collide(objects[0], objects[1]);
}

void GLGame::draw(void) {
  glClear(GL_COLOR_BUFFER_BIT);
  //TODO: tesselate drawing

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x/4, window.x/4, -window.y/2, window.y/2);
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(0, 0, window.x/2, window.y);
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINES);
    glVertex2i( window.x/4-1,-window.y/2);
    glVertex2i( window.x/4-1,-window.y/8);
    glVertex2i( window.x/4-1, window.y/8);
    glVertex2i( window.x/4-1, window.y/2);
  glEnd();
  glTranslatef(-objects[0]->ship->position.x, -objects[0]->ship->position.y, 0.0f);
  starfield->draw(objects[0]->ship->velocity, objects[0]->ship->position);
  objects[0]->draw();
  objects[1]->draw();

  /* PLAYER 2 */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x/4, window.x/4, -window.y/2, window.y/2);
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(window.x/2, 0, window.x/2, window.y);
  //TODO: Refactor somehow as to not look at internals
  glTranslatef(-objects[1]->ship->position.x, -objects[1]->ship->position.y, 0.0f);
  //TODO: Refactor into loop
  starfield->draw(objects[1]->ship->velocity, objects[1]->ship->position);
  objects[0]->draw();
  objects[1]->draw();

  /* VIEWPORT */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  // TODO: make world (0,width,0,height)
  gluOrtho2D(-world.x, world.x, -world.y, world.y);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glViewport(window.x*3/8, window.y*3/8, window.x/4, window.y/4);
  glColor3f(0.0f,0.0f,0.0f);
  glBegin(GL_POLYGON);
    glVertex2i( -world.x, world.y);
    glVertex2i(  world.x, world.y);
    glVertex2i(  world.x,-world.y);
    glVertex2i( -world.x,-world.y);
  glEnd();
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINE_LOOP);
    glVertex2i( -world.x, world.y);
    glVertex2i(  world.x, world.y);
    glVertex2i(  world.x,-world.y);
    glVertex2i( -world.x,-world.y);
  glEnd();
  objects[0]->draw();
  objects[1]->draw();

  glutSwapBuffers();
}

void GLGame::keyboard (unsigned char key, int x, int y) {
  std::vector<GLShip*>::iterator object;
  for(object = objects.begin(); object != objects.end(); object++) {
    (*object)->input(key);
  }
}
void GLGame::keyboard_up (unsigned char key, int x, int y) {
  std::vector<GLShip*>::iterator object;
  for(object = objects.begin(); object != objects.end(); object++) {
    (*object)->input(key, false);
  }
}

void GLGame::resize(float x, float y) {
  window = Point(x, y);
}

void GLGame::init(int argc, char** argv, float width, float height) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
  glutInitWindowSize(width, height);
  glutCreateWindow("Asteroids");

  glPointSize(1.33f);
  glLineWidth(1.33f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_POINT_SMOOTH);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
}

void GLGame::run(void) {
  last_tick = glutGet(GLUT_ELAPSED_TIME);
  glutMainLoop();
}
