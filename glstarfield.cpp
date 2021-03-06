#include "glstarfield.h"


#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

const int GLStarfield::NUM_REAR_LAYERS = 10;
const int GLStarfield::NUM_FRONT_LAYERS = 5;
const float GLStarfield::STAR_DENSITY = 0.000015;

GLStarfield::GLStarfield(Point const size) {
  point_layers = glGenLists(NUM_REAR_LAYERS + NUM_FRONT_LAYERS + 1);
  int red, green;
  for(int i = 0; i < NUM_REAR_LAYERS + NUM_FRONT_LAYERS + 1; i++) {
    glNewList(point_layers+i, GL_COMPILE);
    glPointSize(4.0f);
    glBegin(GL_POINTS);

    int num_stars = size.x()*size.y()*STAR_DENSITY;
    for(int j = 0; j < num_stars; j++) {
      red = rand()%100;
      green = red > 0 ? rand()%red : 0;
      glColor4f(red/100.0,green/100.0,rand()%100/100.0,rand()%50/100.0+0.2);
      glVertex3f(rand()%(int)size.x(), rand()%(int)size.y(), (i-NUM_REAR_LAYERS)*100);
    }
    glEnd();
    glEndList();
  }
}

GLStarfield::~GLStarfield() {
  glDeleteLists(point_layers, NUM_REAR_LAYERS + NUM_FRONT_LAYERS + 1);
}

void GLStarfield::draw_rear(Point const viewpoint) const {
  for(int i = 0; i < NUM_REAR_LAYERS; i++) {
    glCallList(point_layers+i);
  }
  glCallList(point_layers+NUM_REAR_LAYERS);
}

void GLStarfield::draw_front(Point const viewpoint) const {
  for(int i = 0; i < NUM_FRONT_LAYERS; i++) {
    glCallList(point_layers + NUM_REAR_LAYERS + 1 + i);
  }
}
