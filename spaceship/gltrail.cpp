#include "gltrail.h"
#include "ship.h"
#include "bullet.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <deque>

using namespace std;

GLTrail::GLTrail(Ship* ship) {
  this->ship = ship;
}

void GLTrail::draw() {
  deque<Bullet*>::iterator p;
  for(p = trail.begin(); p != trail.end(); p++) {
  	glBegin(GL_POINTS);
  		glVertex3f((*p)->position.x, (*p)->position.y, 0.0f);
  	glEnd();
  }
}

void GLTrail::step(float delta) {
  deque<Bullet*>::iterator t;
  for(t = trail.begin(); t != trail.end(); t++) {
	(*t)->step(delta);
  }
  add();
}

void GLTrail::add() {
  if(ship->thrusting) {
    trail.push_back(  //TODO: scatter points
      new Bullet(ship->tail(), ship->facing*ship->thrust_force*-5 + ship->velocity, ship->world_size, 1.0)
    );
  }
}