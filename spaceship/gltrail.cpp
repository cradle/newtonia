#include "gltrail.h"
#include "ship.h"
#include "bullet.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <math.h>
#include <deque>

using namespace std;

GLTrail::GLTrail(Ship* ship, float deviation, Point offset, float speed, float rotation, int type)
 : ship(ship), deviation(deviation), offset(offset), speed(speed), rotation(rotation), type(type) {}

void GLTrail::draw() {
  deque<Bullet*>::iterator p;
  glBegin(GL_POINTS);
  for(p = trail.begin(); p != trail.end(); p++) {
      glColor4f(0.7,0.7,0.7,(*p)->aliveness());
  		glVertex2fv((*p)->position);
  }
	glEnd();
}

void GLTrail::step(float delta) {
  deque<Bullet*>::iterator t = trail.begin();
  while(t != trail.end()) {
    (*t)->step(delta);
    if(!(*t)->is_alive()) {
      delete *t;
      t = trail.erase(t);
    } else {
      t++;
    }
  }
  if(type & THRUSTING && ship->thrusting ||
     type & REVERSING && ship->reversing ||
     type & LEFT      && ship->rotation_direction == Ship::LEFT ||
     type & RIGHT     && ship->rotation_direction == Ship::RIGHT) {
       add();
   }
}

void GLTrail::add() {
  Point velocity;
  Point position;
  Point direction;
  //TODO: cross product or something?
  position = ship->tail() + ship->facing * offset.y() + ship->facing.perpendicular() * offset.x();
  direction = ship->facing*-1.0;
  direction.rotate(rotation);
  velocity = direction*speed + ship->velocity;
  velocity.rotate((rand() / (float)RAND_MAX) * deviation - deviation / 2.0);
  trail.push_back(
    new Bullet(position, velocity, 2000.0)
  );
}
