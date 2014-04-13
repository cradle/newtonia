#include "gltrail.h"
#include "ship.h"
#include "particle.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <math.h>
#include <list>

using namespace std;

GLTrail::GLTrail(Ship* ship, float deviation, Point offset, float speed, float rotation, int type, float life)
 : type(type), ship(ship), offset(offset), deviation(deviation), rotation(rotation), speed(speed), life(life) {}
 
GLTrail::~GLTrail() {
  while(!trail.empty()) {
    delete trail.back();
    trail.pop_back();
  }
}

void GLTrail::draw() {
  list<Particle*>::iterator p;
  glBegin(GL_POINTS);
  for(p = trail.begin(); p != trail.end(); p++) {
      glColor4f(0.5,0.5,0.5,(*p)->aliveness());
  		glVertex2fv((*p)->position);
  }
	glEnd();
}

void GLTrail::step(float delta) {
  list<Particle*>::iterator t = trail.begin();
  while(t != trail.end()) {
    (*t)->step(delta);
    if(!(*t)->is_alive()) {
      delete *t;
      t = trail.erase(t);
    } else {
      t++;
    }
  }
  if(type & ALWAYS ||
     (type & THRUSTING && ship->thrusting) ||
     (type & REVERSING && ship->reversing) ||
     (type & LEFT      && ship->rotation_direction == Ship::LEFT) ||
     (type & RIGHT     && ship->rotation_direction == Ship::RIGHT)) {
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
    new Particle(position, velocity, life)
  );
}
