#include "glgame.h"
#include "glship.h"
#include "glcar.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <iostream>
#include <vector>

GLGame::GLGame(float width, float height) : world_width(width), world_height(height) {
  GLShip* object = new GLShip(-width*3/4,-height*3/4);
  object->set_keys('a','d','w',' ');
  objects.push_back(object);

  object = new GLCar(width*3/4,height*3/4);
  object->set_keys('j','l','i','/');
  objects.push_back(object);

  //TODO: use boost foreach
  std::vector<GLShip*>::iterator o;
  for(o = objects.begin(); o != objects.end(); o++) {
    //TODO: find out how to use vectors better
    (*o)->resize(width, height);
  }
}

GLGame::~GLGame() {
  //TODO: Make erase, use boost::ptr_vector
  std::cout << "destructored" << std::endl;
  GLShip* object;
  while(!objects.empty()) {
    object = objects.back();
    objects.pop_back();
    delete object;
  }
  // std::erase(std::remove_if(v.begin(),v.end(),true), v.end());
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

void GLGame::resize(int width, int height) {
  window_width = width;
  window_height = height;
}

void GLGame::draw(void) {
  glClear(GL_COLOR_BUFFER_BIT);
  //TODO: tesselate drawing

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window_width/4, window_width/4, -window_height/2, window_height/2);
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(0, 0, window_width/2, window_height);
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINES);
    glVertex2i( window_width/4-1,-window_height/2);
    glVertex2i( window_width/4-1,-window_height/8);
    glVertex2i( window_width/4-1, window_height/8);
    glVertex2i( window_width/4-1, window_height/2);
  glEnd();
  glTranslatef(-objects[0]->ship->position.x, -objects[0]->ship->position.y, 0.0f);
  objects[0]->draw();
  objects[1]->draw();

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window_width/4, window_width/4, -window_height/2, window_height/2);
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(window_width/2, 0, window_width/2, window_height);
  glTranslatef(-objects[1]->ship->position.x, -objects[1]->ship->position.y, 0.0f);
  objects[0]->draw();
  objects[1]->draw();

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  // TODO: make world (0,width,0,height)
  gluOrtho2D(-world_width, world_width, -world_height, world_height);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glViewport(window_width*3/8, window_height*3/8, window_width/4, window_height/4);
  //TODO: some sort of translucency or clear viewport so world not visible through it
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINE_LOOP);
    glVertex2i( -world_width, world_height);
    glVertex2i(  world_width, world_height);
    glVertex2i(  world_width,-world_height);
    glVertex2i( -world_width,-world_height);
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

void GLGame::init(int argc, char** argv, float width, float height) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
  window_width = width;
  window_height = height;
  glutInitWindowSize(width, height);
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
