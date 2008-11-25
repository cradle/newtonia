#include "glgame.h"
#include "glship.h"
#include "glenemy.h"
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

GLGame::GLGame(float width, float height) : world(Point(width, height)) {}

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

  time_until_next_step -= (current_time - last_tick);

  std::vector<GLShip*>::iterator o;
  while(time_until_next_step <= 0) {
    for(o = objects.begin(); o != objects.end(); o++) {
      (*o)->step(step_size);
    }
    time_until_next_step += step_size;
  }

  last_tick = current_time;
  glutPostRedisplay();

  //yay n^2
  std::vector<GLShip*>::iterator o2;
  for(o = objects.begin(); o != objects.end(); o++) {
    for(o2 = (o+1); o2 != objects.end(); o2++) {
      GLShip::collide(*o, *o2);
    }
  }
}

void GLGame::draw_objects(bool minimap) {
  std::vector<GLShip*>::iterator o;
  for(o = objects.begin(); o != objects.end(); o++) {
    glPushMatrix();
    (*o)->draw(minimap);
    glPopMatrix();
  }  
}

void GLGame::draw(void) {
  glClear(GL_COLOR_BUFFER_BIT);
  //TODO: tesselate drawing

  draw_world(objects[0], true);
  draw_world(objects[1], false);
  draw_map();

  glutSwapBuffers();
}

void GLGame::draw_world(GLShip *glship, bool primary) {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x()/4, window.x()/4, -window.y()/2, window.y()/2);
  glMatrixMode(GL_MODELVIEW);
  
  glLoadIdentity();
  glViewport((primary ? 0 : (window.x()/2)), 0, window.x()/2, window.y());

  glTranslatef(-glship->ship->position.x(), -glship->ship->position.y(), 0.0f);
  starfield->draw(glship->ship->velocity, glship->ship->position);
  draw_objects();  
}

void GLGame::draw_map() {  
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-world.x(), world.x(), -world.y(), world.y());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
    /* DRAW CENTER LINE */  
  glViewport(-window.x(), -window.y(), window.x(), window.y());
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINES);
    glVertex2i(0,-window.y());
    glVertex2i(0, window.y());
  glEnd();
  /* MINIMAP */
  glViewport(window.x()*3/8, window.y()*3/8, window.x()/4, window.y()/4);
  glColor3f(0.0f,0.0f,0.0f);
  /* BLACK BOX OVER MINIMAP */
  glBegin(GL_POLYGON);
    glVertex2i( -world.x(), world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(),-world.y());
    glVertex2i( -world.x(),-world.y());
  glEnd();
  /* LINE AROUND MINIMAP */
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINE_LOOP);
    glVertex2i( -world.x(), world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(),-world.y());
    glVertex2i( -world.x(),-world.y());
  glEnd();
  draw_objects(true);
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

  glPointSize(2.0f);
  glLineWidth(1.33f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_POINT_SMOOTH);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
}

void GLGame::run(void) {
  GLShip* object = new GLShip(-world.x()*3/4,-world.y()*3/4);
  object->set_keys('a','d','w',' ','s');
  objects.push_back(object);

  object = new GLCar(world.x()*3/4,world.y()*3/4);//, objects[0]);
  object->set_keys('j','l','i','/','k');
  objects.push_back(object);
  
  for(int i = 0; i < 10; i++) {
    objects.push_back(new GLEnemy(rand()%(int)(world.x()*2), rand()%(int)(world.y()*2), objects[1]));
    objects.push_back(new GLEnemy(rand()%(int)(world.x()*2), rand()%(int)(world.y()*2), objects[0]));
  }
  
  WrappedPoint::set_boundaries(world);
  
  starfield = new GLStarfield(world);
  
  last_tick = glutGet(GLUT_ELAPSED_TIME);
  time_until_next_step = 0;
  glutMainLoop();
}
