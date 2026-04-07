#include "gltrail.h"
#include "ship.h"

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
  if (trail.empty()) return;

  float cr = 1.0f - ship->color[0];
  float cg = 1.0f - ship->color[1];
  float cb = 1.0f - ship->color[2];

  MeshBuilder mb;
  mb.begin(GL_POINTS);
  for (const TrailPoint& p : trail) {
    mb.color(cr, cg, cb, p.aliveness());
    mb.vertex(p.x, p.y);
  }
  mb.end();

  mesh_.upload(mb, GL_DYNAMIC_DRAW);
  mesh_.draw(point_size);
}

void GLTrail::step(float delta) {
  for (size_t i = 0; i < trail.size(); ) {
    trail[i].step(delta);
    if (!trail[i].is_alive()) {
      trail[i] = std::move(trail.back());
      trail.pop_back();
    } else {
      ++i;
    }
  }
  if (type & ALWAYS ||
      (type & THRUSTING && ship->ship->thrusting) ||
      (type & REVERSING && ship->ship->reversing) ||
      (type & LEFT      && ship->ship->rotation_direction == Ship::LEFT) ||
      (type & RIGHT     && ship->ship->rotation_direction == Ship::RIGHT)) {
    int current_time = glutGet(GLUT_ELAPSED_TIME);
    if (last_add_time + add_interval < current_time) {
      add();
      last_add_time = current_time;
    }
  }
}

void GLTrail::add() {
  Point pos = ship->ship->tail()
            + ship->ship->facing * offset.y()
            + ship->ship->facing.perpendicular() * offset.x();

  Point dir = ship->ship->facing * -1.0f;
  dir.rotate(rotation);

  Point vel = dir * speed + ship->ship->velocity;
  vel.rotate((rand() / (float)RAND_MAX) * deviation - deviation / 2.0f);

  trail.emplace_back(pos.x(), pos.y(), vel.x(), vel.y(), life);
}
