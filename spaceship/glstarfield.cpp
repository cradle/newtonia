#include "glstarfield.h"


#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

GLStarfield::GLStarfield(Point size) {
  point_layers = glGenLists(1);
  for(int i = 0; i < NUM_REAR_LAYERS + NUM_FRONT_LAYERS + 1; i++) {
    glNewList(point_layers+i, GL_COMPILE);
    glBegin(GL_POINTS);
    for(int j = 0; j < NUM_STARS; j++) {
      glVertex2f((rand()%(int)size.x()*2.0 - size.x()), (rand()%(int)size.y()*2.0 - size.y()));
    }
    glEnd();
    glEndList();
  }
}

void GLStarfield::draw(Point velocity, Point viewpoint) {
  glPushMatrix();
  for(int i = 0; i < NUM_REAR_LAYERS; i++) {
    float c = 1-i/(float)NUM_REAR_LAYERS;
    glColor3f(c,c,c);
    glTranslatef(viewpoint.x()/(float)(NUM_REAR_LAYERS+1), viewpoint.y()/(float)(NUM_REAR_LAYERS+1), 0.0f);
    glCallList(point_layers+i);
  }
  glPopMatrix();

  glColor3f(1.0,1.0,1.0);
  glCallList(point_layers+NUM_REAR_LAYERS);

  glPushMatrix();
  for(int i = 0; i < NUM_FRONT_LAYERS; i++) {
    glTranslatef(-viewpoint.x()/2.0, -viewpoint.y()/2.0, 0.0f);
    glScalef(2, 2, 1);
    glRotatef(0.1,0,0,1.0f);
    glCallList(point_layers + NUM_FRONT_LAYERS + 1);
  }
  glPopMatrix();
}
