#include "glship.h"
#include "ship.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <deque>
#include <iostream>

using namespace std;

GLShip::GLShip(int x, int y) {
  //TODO: load config from file (colours too)
  ship = new Ship(x, y);
  trails.push_back(new deque<Bullet*>);
}

GLShip::~GLShip() {
  delete ship;
}

void GLShip::collide(GLShip* first, GLShip* second) {
  Ship::collide(first->ship, second->ship);
}

void GLShip::step(float delta) {
  //TODO: decouple timestep
  
  //TODO: REALLLY NEED TO ABSTRACT THIS STUPID LOOP, and move into ship/trail
  deque<deque<Bullet*>*>::iterator trail;
  for(trail = trails.begin(); trail != trails.end(); trail++) {
    for(deque<Bullet*>::iterator p = (*trail)->begin(); p != (*trail)->end(); p++) {
      (*p)->step(delta);
    }
  }
  
  ship->step(delta);
  if(ship->thrusting) {
    //TODO: push into trail
    trails.back()->push_back(
      //TODO: Add some variance to trail if using dots
      new Bullet(ship->tail(), ship->facing*ship->thrust_force*-5 + ship->velocity, world, 1.0)
    ); // opposing force/mass
  }
  
  
  //TODO: kill dead
  // Bullet* b;
  // while(!trails.front()->empty() && !trails.front()->front()->is_alive()) {
  //   b = trails.front()->front();
  //   trails.front()->pop_front();
  //   delete b;
  //   // if(trails.front() != trails.back() && trails.front()->empty()) {
  //   //   delete trails.front();
  //   //   trails.pop_front();
  //   // }
  // }
  // while(!trails.front()->empty() && !trails.front()->front()->is_alive()) {
  //   delete trails.front()->front();
  //   trails.front()->pop_front();
  //   if(trails.size() > 1 && trails.front()->empty()) {
  //     delete trails.front();
  //     trails.pop_front();
  //   }
  // }
}

void GLShip::resize(Point world_size) {
  world = world_size;
  ship->set_world_size(world);
  for(vector<Bullet>::iterator bullet = ship->bullets.begin(); bullet != ship->bullets.end(); bullet++) {
    bullet->set_world_size(world);
  }
}

void GLShip::set_keys(int left, int right, int thrust, int shoot) {
  left_key = left;
  right_key = right;
  shoot_key = shoot;
  thrust_key = thrust;
}

void GLShip::input(unsigned char key, bool pressed) {
  if(!ship->is_alive())
    return;
  if (key == left_key) {
    ship->rotate_left(pressed);
  } else if (key == right_key) {
    ship->rotate_right(pressed);
  } else if (key == thrust_key) {
    if(!pressed) {
      trails.push_back(new deque<Bullet*>);
    }
    ship->thrust(pressed);
  } else if (key == shoot_key && pressed) {
    ship->shoot();
  }
}

void GLShip::draw() {
  //TODO: I want ship trails
  //First try it with line strips, there may be a (very) slight performance increase. Also disable all unnecessary modes like lighting, blending etc.
  //Second, place everything in a display list & use that. I've never noticed a big increase worth writing home about with this method either though.
  //Last, try it using compiled vertex buffers or even better, hardware supported ones. This is the best performance gain you'll get.
  
  glPushMatrix();
  glTranslatef(ship->position.x, ship->position.y, 0.0f);
  glScalef( ship->width, ship->height, 1.0f);

  if(ship->is_alive()) {
    glColor3f( 1.0f, 1.0f, 1.0f );
  } else {
    glColor3f( 1.0f, 1.0f, 0.0f );
  }

  //TODO: rotatei could be used with degrees?
  glRotatef( ship->heading(), 0.0f, 0.0f, 1.0f);


  //TODO: Abstract into 'shape' class/struct (or similar)
  // eg: class Shape() {void draw() (?); type = GL_LINE_LOOP; points = [[0,0,0], [1,1,1]]}
	glBegin(GL_LINE_LOOP);						// Drawing The Ship
	  // TODO: Use vectors (arrays) and display lists
	  // http://cgm.cs.mcgill.ca/~msuder/courses/557/tutorial/4.c
	  // glVertex2fv(point);
		glVertex2f( 0.0f, 1.0f);				// Top
		glVertex2f(-0.8f,-1.0f);				// Bottom Left
		glVertex2f( 0.0f,-0.5f);				// Bottom Middle
		glVertex2f( 0.8f,-1.0f);				// Bottom Right
	glEnd();							// Finished Drawing The Ship

	if(ship->thrusting) {
  	glBegin(GL_QUADS);						// Drawing The Flame
  		glVertex2f( 0.0f,-0.5f );				// Top
  		glVertex2f(-0.4f,-0.75f );				// Left
  		glVertex2f( 0.0f,-1.5f );				// Bottom
  		glVertex2f( 0.4f,-0.75f );				// Right
  	glEnd();							// Finished Drawing The Flame
	}

  glPopMatrix();

  //TODO: abstract to GLTrail
  deque<deque<Bullet*>*>::iterator trail;
  for(trail = trails.begin(); trail != trails.end(); trail++) {
    //TODO: Use line strip (or scatter points?) but make world wrap nice
  	glBegin(GL_POINTS);
    for(deque<Bullet*>::iterator p = (*trail)->begin(); p != (*trail)->end(); p++) {
      //TODO: Work out how to make bullets draw themselves. GLBullet?
  		glVertex3f((*p)->position.x, (*p)->position.y, 0.0f);
    }
  	glEnd();
  }

  glColor3f(1,1,1);
	glBegin(GL_POINTS);
    for(vector<Bullet>::iterator bullet = ship->bullets.begin(); bullet != ship->bullets.end(); bullet++) {
      //TODO: Work out how to make bullets draw themselves. GLBullet?
  		glVertex3f(bullet->position.x, bullet->position.y , 0.0f);
    }
	glEnd();
}
