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

GLTrail::GLTrail(Ship* ship, float deviation, float offset, float speed)
 : ship(ship), deviation(deviation), offset(offset), speed(speed) {}

void GLTrail::draw() {
  Bullet* last = *trail.begin();
  deque<Bullet*>::iterator p;
  glBegin(GL_POINTS);
  for(p = trail.begin(); p != trail.end(); p++) {
      glColor4f(1,1,1,(*p)->aliveness());
  		glVertex2fv((*p)->position);
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
  if(ship->thrusting) {
    add();
  }
}

void GLTrail::add() {
  Point velocity;
  Point position;
  position = ship->tail() + ship->facing.perpendicular() * offset;
  velocity = ship->facing*-1.0*speed + ship->velocity;
  velocity.rotate((rand() / (float)RAND_MAX) * deviation - deviation / 2.0);
  trail.push_back( 
    new Bullet(position, velocity, 2000.0)
  );
}