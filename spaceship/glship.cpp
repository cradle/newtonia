#include "glship.h"
#include "ship.h"

#include <OpenGL/gl.h>		

#include <vector>
#include <iostream>

GLShip::GLShip(int x, int y) {
  ship = Ship(x, y);
}

void GLShip::step(float delta) {
  ship.step(delta);
}

void GLShip::resize(float width, float height) {
  ship.set_world_size(width, height);
  window_width = width;
  window_height = height;
  
  for(std::vector<Bullet>::iterator bullet = ship.bullets.begin(); bullet != ship.bullets.end(); bullet++) {
    bullet->set_world_size(width, height);
  }
}

void GLShip::input(unsigned char key, bool pressed) {
  switch(key) {
    case 27: // ESC
      exit(0);
		case 'a':
      ship.rotate_left(pressed);
      break;
    case 'd':
      ship.rotate_right(pressed);
      break;		
    case 'w':
      ship.thrust(pressed);
      break;
    case 32: // spacebar
      if(pressed)
        ship.shoot();
      break;
	}
}

void GLShip::draw() { 
    
  glTranslatef(ship.position.x, ship.position.y, 0.0f);
  //TODO: Doesn't take into account heading
  glScalef( ship.width, ship.height, 1.0f);
  
  glColor3f( 1.0f, 1.0f, 1.0f );

  glRotatef( ship.heading(), 0.0f, 0.0f, 1.0f);
  
  
  //TODO: Abstract into 'shape' class/struct (or similar)
  // eg: class Shape() {void draw() (?); type = GL_LINE_LOOP; points = [[0,0,0], [1,1,1]]}
	glBegin(GL_LINE_LOOP);						// Drawing The Ship
		glVertex3f( 0.0f, 1.0f, 0.0f);				// Top
		glVertex3f(-0.8f,-1.0f, 0.0f);				// Bottom Left
		glVertex3f( 0.0f,-0.5f, 0.0f);				// Bottom Middle
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