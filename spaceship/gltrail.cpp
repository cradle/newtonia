#include "gltrail.h"
#include "ship.h"
#include "bullet.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

#include <math.h>
#include <deque>

using namespace std;

GLTrail::GLTrail(Ship* ship, TYPE type, float deviation, float offset)
 : ship(ship), type(type), deviation(deviation), offset(offset) {}

void GLTrail::draw() {
  deque<Bullet*>::iterator p;
  glBegin(type);
  for(p = trail.begin(); p != trail.end(); p++) {
      glColor4f(1,1,1,(*p)->aliveness());
  		glVertex2f((*p)->position.x, (*p)->position.y);
  }
	glEnd();
}

void GLTrail::step(float delta) {
  deque<Bullet*>::iterator t = trail.begin();
  while(t != trail.end()) {
    (*t)->step(delta);
    if(!(*t)->is_alive()) {
      t = trail.erase(t);
    } else {
      t++;
    }
  }
  add();
}

void GLTrail::add() {
  Point velocity;
  Point position;
  if(ship->thrusting) {
    position = ship->tail() + Point(ship->facing.y, -ship->facing.x) * offset;
    velocity = ship->facing*-0.25 + ship->velocity*0.99;
    velocity.rotate((rand() / (float)RAND_MAX) * deviation - deviation / 2.0);
    trail.push_back( 
      new Bullet(position, velocity, ship->world_size, 2000.0)
    );
  }
}