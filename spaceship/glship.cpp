#include "glship.h"
#include "gltrail.h"
#include "ship.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <vector>
#include <iostream>

using namespace std;

GLShip::GLShip(int x, int y) {
  //TODO: load config from file (colours too)
  ship = new Ship(x, y);
  trails.push_back(new GLTrail(ship));
  trails.push_back(new GLTrail(ship, 0.5,-8));
  trails.push_back(new GLTrail(ship, 0.5, 8));
  
  color[1] = color[2] = 0.0;
  color[0] = 1.0;
  
  body = glGenLists(1);
  glNewList(body, GL_COMPILE);
  // glVertex2fv(point);
	glVertex2f( 0.0f, 1.0f);
	glVertex2f(-0.8f,-1.0f);
	glVertex2f( 0.0f,-0.5f);
	glVertex2f( 0.8f,-1.0f);
  glEndList();

  jets = glGenLists(1);
  glNewList(jets, GL_COMPILE);
  glColor3f( 1.0f, 1.0f, 1.0f );
	glBegin(GL_QUADS);
	glVertex2f( 0.0f,-0.5f );	
	glVertex2f(-0.4f,-0.75f );
	glVertex2f( 0.0f,-1.5f );	
	glVertex2f( 0.4f,-0.75f );
	glEnd();
  glEndList();
  
  repulsors = glGenLists(1);
  glNewList(repulsors, GL_COMPILE);
  glColor3f( 1.0f, 1.0f, 1.0f );
	glBegin(GL_QUADS);
	glVertex2f( 0.3f,  0.3f );	
	glVertex2f( 0.6f,  0.9f );
	glVertex2f( 0.9f,  0.9f );	
	glVertex2f( 0.75f, 0.3f );
	glEnd();
  glEndList();
}

GLShip::~GLShip() {
  delete ship;
}

void GLShip::collide(GLShip* first, GLShip* second) {
  Ship::collide(first->ship, second->ship);
}

void GLShip::step(float delta) {
  //TODO: decouple timestep
  ship->step(delta);

  for(vector<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
    (*i)->step(delta);
  }
}

void GLShip::set_keys(int left, int right, int thrust, int shoot, int reverse) {
  left_key = left;
  right_key = right;
  shoot_key = shoot;
  thrust_key = thrust;
  reverse_key = reverse;
}

void GLShip::input(unsigned char key, bool pressed) {
  if(!ship->is_alive())
    return;
  if (key == left_key) {
    ship->rotate_left(pressed);
  } else if (key == right_key) {
    ship->rotate_right(pressed);
  } else if (key == thrust_key) {
    ship->thrust(pressed);
  } else if (key == reverse_key) {
    ship->reverse(pressed);
  } else if (key == shoot_key && pressed) {
    ship->shoot();
  }
}

void GLShip::draw(bool minimap) {
  //First try it with line strips, there may be a (very) slight performance increase. Also disable all unnecessary modes like lighting, blending etc.
  //Last, try it using compiled vertex buffers or even better, hardware supported ones. This is the best performance gain you'll get.  
  if(!minimap) {
    draw_bullets();
    for(vector<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
      (*i)->draw();
    }
  }
  draw_ship();
}

void GLShip::draw_ship() {
  glTranslatef(ship->position.x(), ship->position.y(), 0.0f);
  glScalef( ship->width, ship->height, 1.0f);
  glRotatef( ship->heading(), 0.0f, 0.0f, 1.0f);

	if(ship->thrusting) {
    glCallList(jets);
	}
	if(ship->reversing) {
    glPushMatrix();
    glCallList(repulsors);
    glRotatef(180, 0, 1, 0);
    glCallList(repulsors);
    glPopMatrix();
	}
	
  glColor3f(0,0,0);
  glBegin(GL_POLYGON);
  glCallList(body);
	glEnd();

  ship->is_alive() ? glColor3fv( color ) : glColor3f( 1.0f, 1.0f, 1.0f );
  glBegin(GL_LINE_LOOP);
  glCallList(body);
	glEnd();
	glBegin(GL_POINTS);
  glCallList(body);
	glEnd();
}

void GLShip::draw_bullets() {
  glColor3f(1,1,1);
  glBegin(GL_POINTS);
  for(vector<Bullet>::iterator bullet = ship->bullets.begin(); bullet != ship->bullets.end(); bullet++) {
    //TODO: Work out how to make bullets draw themselves. GLBullet?
		glVertex2fv(bullet->position);
  }
	glEnd();
}
