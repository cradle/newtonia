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

GLTrail::GLTrail(GLShip* ship, float deviation, Point offset, float speed, float rotation, int type, float life)
 : type(type), ship(ship), offset(offset), deviation(deviation), rotation(rotation), speed(speed), life(life) {
   last_add_time = glutGet(GLUT_ELAPSED_TIME);
   point_size = 3.5f;
 }

GLTrail::~GLTrail() {
  while(!trail.empty()) {
    delete trail.back();
    trail.pop_back();
  }
}

void GLTrail::draw() {
  list<Particle*>::iterator p;
  glPointSize(point_size);
  glBegin(GL_POINTS);
  for(p = trail.begin(); p != trail.end(); p++) {
      glColor4f(1.0-ship->color[0], 1.0-ship->color[1], 1.0-ship->color[2],(*p)->aliveness());
  	  glVertex2fv((*p)->position);
  }
  glEnd();
}

void GLTrail::collide_grid(Grid &grid) {
  list<Particle*>::iterator t = trail.begin();
  Object *object;
  while(t != trail.end()) {
    object = grid.collide(*(*t));
    if(object != NULL) {
      delete *t;
      t = trail.erase(t);
    } else {
      t++;
    }
  }
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
     (type & THRUSTING && ship->ship->thrusting) ||
     (type & REVERSING && ship->ship->reversing) ||
     (type & LEFT      && ship->ship->rotation_direction == Ship::LEFT) ||
     (type & RIGHT     && ship->ship->rotation_direction == Ship::RIGHT)) {
       int current_time = glutGet(GLUT_ELAPSED_TIME);
       if(last_add_time + add_interval < current_time) {
          add();
          last_add_time = current_time;
       }
   }
}

void GLTrail::add() {
  Point velocity;
  Point position;
  Point direction;
  //TODO: cross product or something?
  position = ship->ship->tail() + ship->ship->facing * offset.y() + ship->ship->facing.perpendicular() * offset.x();
  direction = ship->ship->facing*-1.0;
  direction.rotate(rotation);
  velocity = direction*speed + ship->ship->velocity;
  velocity.rotate((rand() / (float)RAND_MAX) * deviation - deviation / 2.0);
  Particle *particle = new Particle(position, velocity, life);
  trail.push_back(particle);
}
