#include "glstation.h"
#include <math.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

GLStation::GLStation() {
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
  glCallList(body);
  glColor3f(1,1,1);
  glCallList(body);
  glPopMatrix();
  glRotatef(inner_rotation,0,0,1);
  glScalef(0.8,0.8,1);
  glCallList(body);
}

void GLStation::step(float delta) {
  outer_rotation += outer_rotation_speed * delta;
  inner_rotation += inner_rotation_speed * delta;
}