#include "glstarfield.h"


#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

GLStarfield::GLStarfield(Point size) {
  // for(int i = 0; i < NUM_LAYERS; i++) {
  //   for(int j = 0; j < NUM_STARS; j++) {
  //     stars[i][j] = new Point(
  //       (rand()%(int)size.x*2.0 - size.x), 
  //       (rand()%(int)size.y*2.0 - size.y)
  //     );
  //   }
  // }
  point_layer = glGenLists(1);
  glNewList(point_layer, GL_COMPILE);
  glBegin(GL_POINTS);
  for(int j = 0; j < NUM_STARS; j++) {
    glVertex2f((rand()%(int)size.x*2.0 - size.x), (rand()%(int)size.y*2.0 - size.y));
  }
  glEnd();
  glEndList();
}

void GLStarfield::draw(Point velocity, Point viewpoint) {
  glPushMatrix();
  for(int i = 0; i < NUM_REAR_LAYERS; i++) {
    float c = 1-i/(float)NUM_REAR_LAYERS;
    glColor3f(c,c,c);
    glTranslatef(viewpoint.x/(NUM_REAR_LAYERS+1), viewpoint.y/(NUM_REAR_LAYERS+1), 0.0f);
    glCallList(point_layer);
  }
  glPopMatrix();

  glColor3f(1.0,1.0,1.0);
  glCallList(point_layer);

  glPushMatrix();
  for(int i = 0; i < NUM_FRONT_LAYERS; i++) {
    glTranslatef(-viewpoint.x/2.0, -viewpoint.y/2.0, 0.0f);
    glScalef(2, 2, 1);
    glCallList(point_layer);
  }
  glPopMatrix();  
  
  // for(int i = 1; i < NUM_LAYERS; i++) {
  //   float c = 1/(i/2.0+1) + 0.5;
  //   float x = i*5*velocity.x;
  //   float y = i*5*velocity.y;
  //   glPushMatrix();
  //   glScalef(i, i, i);
  //   glTranslatef(-i*viewpoint.x/NUM_LAYERS, -i*viewpoint.y/NUM_LAYERS, 0.0f);
  //   for(int j = 0; j < NUM_STARS; j++) {
  //     glBegin(GL_LINE_STRIP);
  //     glColor4f(0,0,0,0);
  //     glVertex2f(stars[i][j]->x-x, stars[i][j]->y-y);
  //     glColor4f(c,c,c,c);
  //     glVertex2f(stars[i][j]->x, stars[i][j]->y);
  //     glColor4f(0,0,0,0);
  //     glVertex2f(stars[i][j]->x+x, stars[i][j]->y+y);
  //     glEnd();
  //   }
  //   glPopMatrix();
  // }
}