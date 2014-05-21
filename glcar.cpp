#include "glcar.h"
#include "gltrail.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <iostream>

using namespace std;

GLCar::GLCar(const Grid &grid, bool has_friction) : GLShip(grid, has_friction) {
  ship = new Ship(grid, has_friction);
  trails.clear();
  trails.push_back(new GLTrail(this, 0.01, Point( 4.5,0),0.25, 0.0, GLTrail::THRUSTING | GLTrail::LEFT, 1000.0));
  trails.push_back(new GLTrail(this, 0.01, Point(-4.5,0),0.25, 0.0, GLTrail::THRUSTING | GLTrail::RIGHT, 1000.0));
  trails.push_back(new GLTrail(this, 0.5,  Point(-4,17) ,-0.2, 0.9, GLTrail::REVERSING | GLTrail::RIGHT, 250.0));
  trails.push_back(new GLTrail(this, 0.5,  Point( 4,17) ,-0.2,-0.9, GLTrail::REVERSING | GLTrail::LEFT, 250.0));

  color[0] = 255/255.0;
  color[1] = 69/255.0;
  color[2] = 0/255.0;

  body = glGenLists(1);
  glNewList(body, GL_COMPILE);
  // glVertex2fv(point);
  glVertex2f( 0.35f, 1.0f);
  glVertex2f(-0.35f, 1.0f);
  glVertex2f(-0.8f,-1.0f);
  glVertex2f( 0.8f,-1.0);
  glEndList();

  left_jet = glGenLists(1);
  glNewList(left_jet, GL_COMPILE);
  glColor3f( 1-color[0], 1-color[1], 1-color[2] );
  glBegin(GL_TRIANGLES);
    glVertex2f( 0.8f,-1.0f);
    glVertex2f( 0.4f,-1.75f);
    glVertex2f( 0.0f,-1.0f);
  glEnd();
  glEndList();

  right_jet = glGenLists(1);
  glNewList(right_jet, GL_COMPILE);
  glColor3f( 1-color[0], 1-color[1], 1-color[2] );
  glBegin(GL_TRIANGLES);
    glVertex2f( 0.0f,-1.0f);
    glVertex2f(-0.4f,-1.75f);
    glVertex2f(-0.8f,-1.0f);
  glEnd();
  glEndList();

  jets = glGenLists(1);
  glNewList(jets, GL_COMPILE);
  glCallList(left_jet);
  glCallList(right_jet);
  glEndList();

  genForceShield();
  genRepulsor();
}

GLCar::~GLCar() {
  glDeleteLists(body, 1);
  glDeleteLists(left_jet, 1);
  glDeleteLists(right_jet, 1);
  glDeleteLists(jets, 1);
}

void GLCar::draw_ship(bool minimap) const {
  GLShip::draw_ship(minimap);

  if(!minimap) {
  	if(ship->rotation_direction == Ship::LEFT) {
      glCallList(left_jet);
    } else if (ship->rotation_direction == Ship::RIGHT) {
      glCallList(right_jet);
  	}
	}
}
