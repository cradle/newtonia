#include "asteroid_drawer.h"
#include "object.h"
#include <math.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <iostream>

const int AsteroidDrawer::number_of_segments = 10;

void AsteroidDrawer::draw(Object const *object) {
  glPushMatrix();
  glTranslatef(object->position.x(), object->position.y(), 0.0f);
  glScalef(object->radius, object->radius, 1.0f);
  // glRotatef(object->heading(), 0.0f, 0.0f, 1.0f);
  glColor3f(0.0f, 0.0f, 0.0f);
  glBegin(GL_POLYGON);
  float segment_size = 360.0/number_of_segments, d;
  for (int i = 0; i < 360; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(cos(d),sin(d));
  }
  glEnd();
  glColor3f(1.0f, 1.0f, 1.0f);
  glBegin(GL_LINE_LOOP);
  for (int i = 0; i < 360; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(cos(d),sin(d));
  }
  glEnd();
  glPopMatrix();
}