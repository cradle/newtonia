#include "glstation.h"
#include <math.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include "glship.h"
#include "glenemy.h"
#include <vector>

using namespace std;

GLStation::GLStation(vector<GLShip*>* objects, vector<GLShip*>* targets) : objects(objects), targets(targets) {
  time_between_waves = 10000.0;
  time_until_next_wave = 0.0;

  float segment_size = 360.0/NUM_SEGMENTS;
  outer_rotation_speed = 0.001;
  inner_rotation_speed = -0.00025;
  body = glGenLists(1);
  glNewList(body, GL_COMPILE);
  float r = 2000.0, r2 = 1800.0, d;
  glColor3f(0,0,0);
  glBegin(GL_POLYGON);
  for (int i = 0; i < 360; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(r*cos(d),r*sin(d));
  }
  glEnd();
  glColor3f(1,1,1);
  for (int i = 0; i < 360; i+= segment_size) {
    glBegin(GL_LINE_LOOP);
    d = i*M_PI/180;
    glVertex2f(r*cos(d),r*sin(d));
    glVertex2f(r2*cos(d),r2*sin(d));
    d = (i+segment_size)*M_PI/180;
    glVertex2f(r2*cos(d),r2*sin(d));
    glVertex2f(r*cos(d),r*sin(d));
    glEnd();
  }
  glEndList();
  
  inner_rotation = 0;
  outer_rotation = 0;
}

void GLStation::draw() {
  glPushMatrix();
  glRotatef(outer_rotation,0,0,1);
  glColor3f(0,0,0);
  // glCallList(body);
  glColor3f(1,1,1);
  // glCallList(body);
  glPopMatrix();
  glRotatef(inner_rotation,0,0,1);
  glScalef(0.8,0.8,1);
  // glCallList(body);
}

void GLStation::step(float delta) {
  outer_rotation += outer_rotation_speed * delta;
  inner_rotation += inner_rotation_speed * delta;
  time_until_next_wave -= delta;
  Point pos = Point(1800,0);
  pos.rotate(rand()%360*M_PI/180);
  while(time_until_next_wave <= 0.0) {
    objects->push_back(new GLEnemy(pos.x(), pos.y(), targets));
    time_until_next_wave += time_between_waves;
  }
}
