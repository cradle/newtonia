#include "glgame.h"
#include "glship.h"
#include "glenemy.h"
#include "glcar.h"
#include "glstarfield.h"
#include "wrapped_point.h"
#include "glstation.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <iostream>
#include <vector>

GLGame::GLGame(float width, float height) : world(Point(width, height)) {}

GLGame::~GLGame() {
  //TODO: Make erase, use boost::ptr_vector? something better
  // std::erase(std::remove_if(v.begin(),v.end(),true), v.end());
  GLShip* object;
  while(!objects->empty()) {
    object = objects->back();
    objects->pop_back();
    delete object;
  }
  delete station;
}

void GLGame::tick(void) {
  int current_time = glutGet(GLUT_ELAPSED_TIME);

  time_until_next_step -= (current_time - last_tick);
  
  num_frames++;

  std::vector<GLShip*>::iterator o, o2;
  while(time_until_next_step <= 0) {
    station->step(step_size);
    for(o = objects->begin(); o != objects->end(); o++) {
      (*o)->step(step_size);
      
      station->collide((*o)->ship);
      
      //yay O(n^2)
      for(o2 = (o+1); o2 != objects->end(); o2++) {
        GLShip::collide(*o, *o2);
      }
    }
    
    o = objects->begin();
    while(o != objects->end()) {
      if((*o)->is_removable()) {
        delete *o;
        o = objects->erase(o);
      } else {
        o++;
      }
    }
    
    time_until_next_step += step_size;
  }
  std::cout << (num_frames*1000 / current_time) << std::endl;
  last_tick = current_time;
  glutPostRedisplay();
}

void GLGame::draw_objects(bool minimap) {
  std::vector<GLShip*>::iterator o;
  for(o = objects->begin(); o != objects->end(); o++) {
    glPushMatrix();
    (*o)->draw(minimap);
    glPopMatrix();
  }
  station->draw();  
}

void GLGame::draw(void) {
  glClear(GL_COLOR_BUFFER_BIT);
  
  draw_world((*objects)[0], true);
  draw_world((*objects)[1], false);
  draw_map();

  glutSwapBuffers();
}

void GLGame::draw_world(GLShip *glship, bool primary) {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  float zoom = 1.5;
  gluOrtho2D(-window.x()/4*zoom, window.x()/4*zoom, -window.y()/2*zoom, window.y()/2*zoom);
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport((primary ? 0 : (window.x()/2)), 0, window.x()/2, window.y());

  // Store the rendered world in a display list
  glNewList(gameworld, GL_COMPILE);
    glTranslatef(-glship->ship->position.x(), -glship->ship->position.y(), 0.0f);
    starfield->draw(glship->ship->velocity, glship->ship->position);
    draw_objects();
  glEndList();

  // Draw the world tesselated
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      glPushMatrix();
      glTranslatef(world.x()*2*x, world.y()*2*y, 0.0f);
      glCallList(gameworld);
      glPopMatrix();
    }
  }
}

void GLGame::draw_map() {
  float minimap_size = window.y()/4;
  
    /* DRAW CENTER LINE */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glViewport(0, 0, window.x(), window.y());
  glBegin(GL_LINES);
  glColor4f(1,1,1,0.5);
  glVertex2f(0,-window.y());
  glVertex2f(0,-minimap_size);
  glVertex2f(0, minimap_size);
  glVertex2f(0, window.y());
  glEnd();
    
  /* MINIMAP */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-world.x(), world.x(), -world.y(), world.y());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glViewport(window.x()/2 - minimap_size/2, window.y()/2 - minimap_size/2, minimap_size, minimap_size);
  glColor4f(0.0f,0.0f,0.0f,0.8f);
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
  for(object = objects->begin(); object != objects->end(); object++) {
    (*object)->input(key);
  }
}

void GLGame::keyboard_up (unsigned char key, int x, int y) {
  std::vector<GLShip*>::iterator object;
  for(object = objects->begin(); object != objects->end(); object++) {
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
  objects = new std::vector<GLShip*>;
  players = new std::vector<GLShip*>;
  
  GLShip* object = new GLShip(-world.x()*3/4,-world.y()*3/4);
  object->set_keys('a','d','w',' ','s','x');
  objects->push_back(object);
  players->push_back(object);

  object = new GLCar(world.x()*3/4,world.y()*3/4);//, objects[0]);
  object->set_keys('j','l','i','/','k',',');
  objects->push_back(object);
  players->push_back(object);
  
  station = new GLStation(objects, players);
  
  WrappedPoint::set_boundaries(world);

  starfield = new GLStarfield(world);

  gameworld = glGenLists(1);

  last_tick = glutGet(GLUT_ELAPSED_TIME);
  time_until_next_step = 0;
  num_frames = 0;
  glutMainLoop();
}
