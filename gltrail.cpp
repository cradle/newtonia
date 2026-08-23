#include "gltrail.h"
#include "ship.h"

#include "gl_compat.h"

#include <math.h>
#include <vector>

using namespace std;

GLTrail::GLTrail(GLShip* ship, float deviation, Point offset, float speed, float rotation, int type, float life)
 : type(type), ship(ship), offset(offset), deviation(deviation), rotation(rotation), speed(speed), life(life) {
   since_add = 0.0f;
   point_size = 3.5f;
 }

GLTrail::~GLTrail() {
}

void GLTrail::draw() {
  if (trail.empty()) return;

  float cr = 1.0f - ship->color[0];
  float cg = 1.0f - ship->color[1];
  float cb = 1.0f - ship->color[2];

  // Tiny diamonds, not GL_POINTS: the field GPU that silently dropped the
  // hazard-debris point draw (the seeker saga, 2026-08-23) drops this one
  // too — the exhaust trail vanished mid-thrust while llvmpipe rendered
  // it fine. Near point-sized (2-3 px at typical zoom), shrinking as each
  // puff fades, so the look stays the old soft dotted exhaust.
  MeshBuilder mb;
  mb.begin(GL_TRIANGLES);
  for (const TrailPoint& p : trail) {
    float a = p.aliveness();
    float r = 2.2f * (0.5f + 0.5f * a);
    mb.color(cr, cg, cb, a);
    mb.vertex(p.x - r, p.y); mb.vertex(p.x, p.y + r); mb.vertex(p.x + r, p.y);
    mb.vertex(p.x - r, p.y); mb.vertex(p.x + r, p.y); mb.vertex(p.x, p.y - r);
  }
  mb.end();

  mesh_.upload(mb, GL_DYNAMIC_DRAW);
  mesh_.draw();
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
  // Spawn cadence on SIM time, like the points' own ageing above. It used to
  // read the wall clock, which is the same thing at 60 fps and nothing like it
  // otherwise: under the time-scale debug keys, a replay's 4x fast-forward, or
  // an offline video render (shots/video.sh — where a frame can take a tenth
  // of a second to draw), the world moved at one rate and the exhaust puffed
  // at another. It also made the render non-reproducible, since the trail then
  // depended on how fast the machine happened to be.
  since_add += delta;
  if (type & ALWAYS ||
      (type & THRUSTING && ship->ship->thrusting) ||
      (type & REVERSING && ship->ship->reversing) ||
      (type & LEFT      && ship->ship->rotation_direction == Ship::LEFT) ||
      (type & RIGHT     && ship->ship->rotation_direction == Ship::RIGHT)) {
    if (since_add > add_interval) {
      add();
      // Carry the remainder so the rate holds when a step is longer than the
      // interval, but never bank more than one (a long pause between thrusts
      // must not fire a burst on the first frame of the next one).
      since_add -= add_interval;
      if (since_add > add_interval) since_add = add_interval;
    }
  } else if (since_add > add_interval) {
    since_add = add_interval;  // armed, not accumulating: same as before
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
