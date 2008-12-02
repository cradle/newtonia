#include "glship.h"
#include "gltrail.h"
#include "ship.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <vector>
#include <iostream>

using namespace std;

GLShip::GLShip(int x, int y) {
  //TODO: load config from file (colours too)
  ship = new Ship(x, y);
  trails.push_back(new GLTrail(ship));
  trails.push_back(new GLTrail(ship, 0.5,Point(-4,17),-0.1, 0.9, GLTrail::REVERSING | GLTrail::RIGHT));
  trails.push_back(new GLTrail(ship, 0.5,Point( 4,17),-0.1,-0.9, GLTrail::REVERSING | GLTrail::LEFT));
  
  color[0] = 72/255.0;
  color[1] = 118/255.0;
  color[2] = 255/255.0;
  
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
  ship->step(delta);

  for(vector<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
    (*i)->step(delta);
  }
}

void GLShip::set_keys(int left, int right, int thrust, int shoot, int reverse, int mine) {
  left_key = left;
  right_key = right;
  shoot_key = shoot;
  thrust_key = thrust;
  reverse_key = reverse;
  mine_key = mine;
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
  } else if (key == shoot_key) {
    ship->shoot(pressed);
  } else if (key == mine_key) {
    ship->mine(pressed);
  }
}

void GLShip::draw(bool minimap) {
  //First try it with line strips, there may be a (very) slight performance increase. Also disable all unnecessary modes like lighting, blending etc.
  //Last, try it using compiled vertex buffers or even better, hardware supported ones. This is the best performance gain you'll get.  
  if(!minimap) {
    draw_particles();
    draw_debris();
    for(vector<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
      (*i)->draw();
    }
    draw_mines();
  }
  if(ship->is_alive()) {
    draw_ship(minimap);
  }
}

void GLShip::draw_ship(bool minimap) {
  glTranslatef(ship->position.x(), ship->position.y(), 0.0f);
  glScalef( ship->radius, ship->radius, 1.0f);
  glRotatef( ship->heading(), 0.0f, 0.0f, 1.0f);
  
  if(minimap) {
    glColor3fv(color);
    glBegin(GL_POINTS);
    glVertex2f(0.0f, 0.0f);
    glEnd();
    return;
  }

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
  
  glColor3fv(color);
  glBegin(GL_LINE_LOOP);
  glCallList(body);
	glEnd();
	glBegin(GL_POINTS);
  glCallList(body);
	glEnd();
}

void GLShip::draw_particles() {
  glColor3fv(color);
  glBegin(GL_POINTS);
  for(vector<Particle>::iterator b = ship->bullets.begin(); b != ship->bullets.end(); b++) {
    //TODO: Work out how to make bullets draw themselves. GLBullet?
		glVertex2fv(b->position);
  }
	glEnd();
}

bool GLShip::is_removable() {
  return ship->is_removable();
}

void GLShip::draw_debris() {
  glBegin(GL_POINTS);
  for(vector<Particle>::iterator d = ship->debris.begin(); d != ship->debris.end(); d++) {
    glColor4f(color[0], color[1], color[2], d->aliveness());
		glVertex2fv(d->position);
  }
	glEnd();
}

void GLShip::draw_mines() {
  float size = 20.0;
  for(vector<Particle>::iterator m = ship->mines.begin(); m != ship->mines.end(); m++) {
    glBegin(GL_LINE_STRIP);
    glColor4f(0,0,0,0);
  	glVertex2fv(m->position + Point(0,-size));
    glColor3fv(color);
  	glVertex2fv(m->position);
    glColor4f(0,0,0,0);
  	glVertex2fv(m->position + Point(0,size));
  	glEnd();
    glBegin(GL_LINE_STRIP);
    glColor4f(0,0,0,0);
  	glVertex2fv(m->position + Point(-size,0));
    glColor3fv(color);
  	glVertex2fv(m->position);
    glColor4f(0,0,0,0);
  	glVertex2fv(m->position + Point(size,0));
  	glEnd();
  }
}
