#include "glcar.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <vector>
#include <iostream>

GLCar::GLCar(float x, float y) {
  ship = Car(x,y);
}

void GLCar::draw() {

  glTranslatef(ship.position.x, ship.position.y, 0.0f);
  //TODO: Doesn't take into account heading
  glScalef( ship.width, ship.height, 1.0f);

  if(ship.is_alive()) {
    glColor3f( 1.0f, 1.0f, 1.0f );
  } else {
    glColor3f( 1.0f, 1.0f, 0.0f );
  }

  glRotatef( ship.heading(), 0.0f, 0.0f, 1.0f);


  //TODO: Abstract into 'shape' class/struct (or similar)
  // eg: class Shape() {void draw() (?); type = GL_LINE_LOOP; points = [[0,0,0], [1,1,1]]}
	glBegin(GL_LINE_LOOP);						// Drawing The Ship
		glVertex3f( 0.0f, 1.2f, 0.0f);				// Top
		glVertex3f( 0.0f, 0.8f, 0.0f);				// Top
		glVertex3f(-0.8f,-1.0f, 0.0f);				// Bottom Left
		glVertex3f( 0.8f,-1.0f, 0.0f);				// Bottom Right
	glEnd();							// Finished Drawing The Ship

	if(ship.thrusting) {
  	glBegin(GL_QUADS);						// Drawing The Flame
  		glVertex3f( 0.0f,-0.5f, 0.0f);				// Top
  		glVertex3f(-0.4f,-0.75f, 0.0f);				// Left
  		glVertex3f( 0.0f,-1.5f, 0.0f);				// Bottom
  		glVertex3f( 0.4f,-0.75f, 0.0f);				// Right
  	glEnd();							// Finished Drawing The Flame
	}

  glLoadIdentity();

  glBegin(GL_POINTS);
  for(std::vector<Bullet>::iterator bullet = ship.bullets.begin(); bullet != ship.bullets.end(); bullet++) {
    //TODO: Work out how to make bullets draw themselves. GLBullet?
      glVertex3f(bullet->position.x, bullet->position.y , 0.0f);
  }
  glEnd();
}