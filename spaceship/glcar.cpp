#include "glcar.h"
#include "gltrail.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <iostream>

using namespace std;

GLCar::GLCar(float x, float y) {
  ship = new Car(x,y);
  trails.push_back(new GLTrail(ship, 0.01, Point( 4.5,0),0.25, 0.0, GLTrail::THRUSTING | GLTrail::LEFT));
  trails.push_back(new GLTrail(ship, 0.01, Point(-4.5,0),0.25, 0.0, GLTrail::THRUSTING | GLTrail::RIGHT));
  trails.push_back(new GLTrail(ship, 0.5,  Point(-4,17) ,-0.2, 0.9, GLTrail::REVERSING | GLTrail::RIGHT));
  trails.push_back(new GLTrail(ship, 0.5,  Point( 4,17) ,-0.2,-0.9, GLTrail::REVERSING | GLTrail::LEFT));
  
  color[0] = color[1] = 0.0;
  color[2] = 1.0;
  
  body = glGenLists(1);
  glNewList(body, GL_COMPILE);
  // glVertex2fv(point);
  glVertex2f( 0.3f, 1.0f);
  glVertex2f(-0.3f, 1.0f);
  glVertex2f(-0.8f,-1.0f);
  glVertex2f( 0.8f,-1.0);
  glEndList();
  
  left_jet = glGenLists(1);
  glNewList(left_jet, GL_COMPILE);
  glColor3f( 1.0f, 1.0f, 1.0f );
  glBegin(GL_TRIANGLES);						// Drawing The Flame
    glVertex2f( 0.8f,-1.0f);				// Bottom
    glVertex2f( 0.4f,-1.75f);				// Left
    glVertex2f( 0.0f,-1.0f);				// Top
  glEnd();		
  glEndList();
  
  right_jet = glGenLists(1);
  glNewList(right_jet, GL_COMPILE);
  glColor3f( 1.0f, 1.0f, 1.0f );
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
}

void GLCar::draw_ship() {
  GLShip::draw_ship();

	if(ship->rotation_direction == Ship::LEFT) {
    glCallList(left_jet);
  } else if (ship->rotation_direction == Ship::RIGHT) {
    glCallList(right_jet);
	}
}
