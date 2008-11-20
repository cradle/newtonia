#include "glcar.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <deque>
#include <iostream>

using namespace std;

GLCar::GLCar(float x, float y) {
  ship = new Car(x,y);
  //TODO: pull up heirerachry
  trails.push_back(new deque<Bullet*>);
  trails2.push_back(new deque<Bullet*>);
  //TODO: abstract. and make step better (doesn't disconnect trail)
}

void GLCar::step(float delta) {
  //TODO: PULL UP HEIERACRHY
  //TODO: REALLLY NEED TO ABSTRACT THIS STUPID LOOP, and move into ship/trail
  deque<deque<Bullet*>*>::iterator trail;
  for(trail = trails.begin(); trail != trails.end(); trail++) {
    for(deque<Bullet*>::iterator p = (*trail)->begin(); p != (*trail)->end(); p++) {
      (*p)->step(delta);
    }
  }
  
  for(trail = trails2.begin(); trail != trails2.end(); trail++) {
    for(deque<Bullet*>::iterator p = (*trail)->begin(); p != (*trail)->end(); p++) {
      (*p)->step(delta);
    }
  }
  
  ship->step(delta);
  if(ship->thrusting) {
    //TODO: push into trail
    trails.back()->push_back(
      //TODO: Add some variance to trail if using dots
      new Bullet(ship->tail() + Point(ship->facing.y, -ship->facing.x) * ship->width/2, ship->facing*ship->thrust_force*-5 + ship->velocity, world, 1.0)
    ); // opposing force/mass
    trails2.back()->push_back(
      //TODO: Add some variance to trail if using dots
      new Bullet(ship->tail() + Point(ship->facing.y, -ship->facing.x) * -ship->width/2, ship->facing*ship->thrust_force*-5 + ship->velocity, world, 1.0)
    ); // opposing force/mass
  }
}

void GLCar::draw() {
  glPushMatrix();

  glTranslatef(ship->position.x, ship->position.y, 0.0f);
  //TODO: Doesn't take into account heading
  glScalef( ship->width, ship->height, 1.0f);

  if(ship->is_alive()) {
    glColor3f( 1.0f, 1.0f, 1.0f );
  } else {
    glColor3f( 1.0f, 1.0f, 0.0f );
  }

  glRotatef( ship->heading(), 0.0f, 0.0f, 1.0f);


  //TODO: Abstract into 'shape' class/struct (or similar)
  // eg: class Shape() {void draw() (?); type = GL_LINE_LOOP; points = [[0,0,0], [1,1,1]]}
	glBegin(GL_LINE_LOOP);						// Drawing The Ship
		glVertex3f( 0.3f, 1.0f, 0.0f);				// Top
		glVertex3f(-0.3f, 1.0f, 0.0f);				// Top
		glVertex3f(-0.8f,-1.0f, 0.0f);				// Bottom Left
		glVertex3f( 0.8f,-1.0f, 0.0f);				// Bottom Right
	glEnd();							// Finished Drawing The Ship

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

  glPopMatrix();
  
  //TODO: abstract to GLTrail
  deque<deque<Bullet*>*>::iterator trail;
  for(trail = trails.begin(); trail != trails.end(); trail++) {
    //TODO: Use line strip (or scatter points?) but make world wrap nice
  	glBegin(GL_LINE_STRIP);
    for(deque<Bullet*>::iterator p = (*trail)->begin(); p != (*trail)->end(); p++) {
      //TODO: Work out how to make bullets draw themselves. GLBullet?
  		glVertex3f((*p)->position.x, (*p)->position.y, 0.0f);
    }
  	glEnd();
  }
  //TODO: abstract to GLTrail
  for(trail = trails2.begin(); trail != trails2.end(); trail++) {
    //TODO: Use line strip (or scatter points?) but make world wrap nice
  	glBegin(GL_LINE_STRIP);
    for(deque<Bullet*>::iterator p = (*trail)->begin(); p != (*trail)->end(); p++) {
      //TODO: Work out how to make bullets draw themselves. GLBullet?
  		glVertex3f((*p)->position.x, (*p)->position.y, 0.0f);
    }
  	glEnd();
  }

  glBegin(GL_POINTS);
  glColor3f(1,1,1);
  for(std::vector<Bullet>::iterator bullet = ship->bullets.begin(); bullet != ship->bullets.end(); bullet++) {
    //TODO: Work out how to make bullets draw themselves. GLBullet?
      glVertex3f(bullet->position.x, bullet->position.y , 0.0f);
  }
  glEnd();
}
