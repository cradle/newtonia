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
  trails.push_back(new GLTrail(ship, GLTrail::DOTS, 0.01, 4.5));
  trails.push_back(new GLTrail(ship, GLTrail::DOTS, 0.01,-4.5));
}

void GLCar::input(unsigned char key, bool pressed) {
  GLShip::input(key, pressed);
  if (key == thrust_key && !pressed) {
    vector<GLTrail*>::iterator t = trails.begin();
    for(t = trails.begin(); t != trails.end(); t++) {
      (*t)->split();
    }
  }
}

void GLCar::draw_ship() {
  glPushMatrix();
  glTranslatef(ship->position.x, ship->position.y, 0.0f);
  glScalef( ship->width, ship->height, 1.0f);
  glRotatef( ship->heading(), 0.0f, 0.0f, 1.0f);

  glColor3f( 1.0f, 1.0f, 1.0f );
	if(ship->thrusting || ship->rotation_direction == Ship::LEFT) {
      glBegin(GL_TRIANGLES);						// Drawing The Flame
          glVertex3f( 0.8f,-1.0f, 0.0f);				// Bottom
          glVertex3f( 0.4f,-1.75f, 0.0f);				// Left
          glVertex3f( 0.0f,-1.0f, 0.0f);				// Top
      glEnd();							// Finished Drawing The Flame
    }
    if(ship->thrusting || ship->rotation_direction == Ship::RIGHT) {
      glBegin(GL_TRIANGLES);						// Drawing The Flame
          glVertex3f( 0.0f,-1.0f, 0.0f);				// Top
          glVertex3f(-0.4f,-1.75f, 0.0f);				// Left
          glVertex3f(-0.8f,-1.0f, 0.0f);				// Top
      glEnd();							// Finished Drawing The Flame
	}

  if(ship->is_alive()) {
    glColor3f( 0.0f, 0.0f, 1.0f );
  } else {
    glColor3f( 1.0f, 1.0f, 1.0f );
  }
  
	glBegin(GL_LINE_LOOP);						// Drawing The Ship
		glVertex3f( 0.3f, 1.0f, 0.0f);				// Top
		glVertex3f(-0.3f, 1.0f, 0.0f);				// Top
		glVertex3f(-0.8f,-1.0f, 0.0f);				// Bottom Left
		glVertex3f( 0.8f,-1.0f, 0.0f);				// Bottom Right
	glEnd();							// Finished Drawing The Ship

  glPopMatrix();
}
