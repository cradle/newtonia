#include "glstation.h"
#include <math.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

GLStation::GLStation() {
  body = glGenLists(1);
  glNewList(body, GL_COMPILE);
  float r = 2000.0, x, y, d;
  for (int i = 0; i < 360; i++)
  {
    d = i*3.14159/180.0;
    glVertex2f(r*cos(d),r*sin(d));
  }
  glEndList();
}

void GLStation::draw() {
  glColor3f(0,0,0);
  glBegin(GL_POLYGON);
  glCallList(body);
  glEnd();

  glColor3f(1,1,1);
  glBegin(GL_LINE_LOOP);
  glCallList(body);
  glEnd();
}