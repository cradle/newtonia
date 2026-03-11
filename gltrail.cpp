#include "gltrail.h"
#include "ship.h"
#include "particle.h"

#include "gl_compat.h"

#include <math.h>
#include <vector>

using namespace std;

GLTrail::GLTrail(GLShip* ship, float deviation, Point offset, float speed, float rotation, int type, float life)
 : type(type), ship(ship), offset(offset), deviation(deviation), rotation(rotation), speed(speed), life(life) {
   last_add_time = glutGet(GLUT_ELAPSED_TIME);
   point_size = 3.5f;
 }

GLTrail::~GLTrail() {
}

void GLTrail::draw() {
  glPointSize(point_size);
  glBegin(GL_POINTS);
  for(size_t i = 0; i < trail.size(); i++) {
      glColor4f(1.0-ship->color[0], 1.0-ship->color[1], 1.0-ship->color[2], trail[i].aliveness());
  	  glVertex2fv(trail[i].position);
  }
  glEnd();
}

void GLTrail::step(float delta) {
  for(size_t i = 0; i < trail.size(); ) {
    trail[i].step(delta);
    if(!trail[i].is_alive()) {
      trail[i] = std::move(trail.back());
      trail.pop_back();
    } else {
      ++i;
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
  trail.push_back(Particle(position, velocity, life));
}
