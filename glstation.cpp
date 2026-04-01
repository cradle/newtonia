#include "glstation.h"
#include <math.h>
#include <cstdlib>

#include "gl_compat.h"

#include "ship.h"
#include "glenemy.h"
#include <list>
#include <iostream>
#include "savegame.h"
#include "grid.h"
#include "follower.h"
// #include "follower.h"

using namespace std;

GLStation::GLStation(const Grid &grid, list<GLShip*>* objects, list<GLShip*>* targets, list<Object*>* asteroids) : Ship(grid, false), objects(objects), targets(targets), asteroids(asteroids) {
  position = Point(0,0);
  radius = 200.0;
  time_until_respawn = 0;
  velocity = Point(0.1f, 0.0f);
  radius_squared = radius * radius;
  max_ships_per_wave = 50;
  extra_ships_per_wave = 1;
  ships_left_to_deploy = ships_this_wave = targets->size();
  time_until_next_ship = 5000;
  time_between_ships = 500;
  deploying = true;
  redeploying = false;
  wave = difficulty = 0;
  lives = 1;
  health = 100;
  alive = true;

  // behaviours.push_back(new Roamer(this));

  outer_rotation_speed = 0.01;
  inner_rotation_speed = -0.0025;
  inner_rotation = outer_rotation = 0;

  body = glGenLists(1);
  glNewList(body, GL_COMPILE);
  float r = radius, r2 = radius * 0.9, d;
  glColor3f(0,0,0);
  glBegin(GL_POLYGON);
  float segment_size = 360.0/NUM_SEGMENTS;
  for (int i = 0; i < 360; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(r*cos(d),r*sin(d));
  }
  glEnd();
  glColor3f(1,1,1);
  for (int i = 0; i < 360; i+= segment_size) {
    glBegin(GL_LINE_LOOP);
    d = i*M_PI/180;
    glVertex2f(r*cos(d),r*sin(d));
    glVertex2f(r2*cos(d),r2*sin(d));
    d = (i+segment_size)*M_PI/180;
    glVertex2f(r2*cos(d),r2*sin(d));
    glVertex2f(r*cos(d),r*sin(d));
    glEnd();
  }
  glEndList();

  map_body = glGenLists(1);
  glNewList(map_body, GL_COMPILE);
  glColor3f(1,1,1);
  glBegin(GL_POLYGON);
  segment_size = 360.0/8;
  for (int i = 0; i < 360; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(r*cos(d),r*sin(d));
  }
  glEnd();
  glEndList();
}

GLStation::~GLStation() {
  // delete targets;
  // delete objects;
  glDeleteLists(body, 1);
  glDeleteLists(map_body, 1);
}

void GLStation::reset(bool was_killed) {
  while(!objects->empty()) {
    if(objects->back()->ship->is_alive()) {
      ships_left_to_deploy++;
    }
    delete objects->back();
    objects->pop_back();
  }
  time_until_next_ship = 5000;
  deploying = redeploying = true;
}

void GLStation::hit() {
  if (!alive) return;
  if (--health <= 0)
    destroy();
}

void GLStation::destroy() {
  alive = false;
  lives = 0;
  // Large radial burst: 300 particles, all guaranteed to expand beyond radius.
  // Speed 0.15–0.60 units/ms × TTL 1500–2500ms → min reach 225 units > 200.
  int count = 300;
  debris.reserve(debris.size() + count);
  for (int i = 0; i < count; i++) {
    float angle = (float)(rand() % 100000) / 100000.0f * 2.0f * (float)M_PI;
    float dist  = (float)(rand() % (int)radius);
    Point start(position.x() + dist * cosf(angle),
                position.y() + dist * sinf(angle));
    float speed = 0.15f + (float)(rand() % 100) / 222.0f;
    Point vel(speed * cosf(angle), speed * sinf(angle));
    debris.push_back(Particle(start, vel, 1500.0f + rand() % 1000));
  }
}

int GLStation::level() const {
  return wave;
}

void GLStation::draw(bool minimap) const {
  glPushMatrix();
  glTranslatef(position.x(), position.y(), 0);

  if(minimap && alive) {
    glColor3f(1.0f, 0.8f, 0.0f);
    glCallList(map_body);
  } else if(alive) {
    glLineWidth(2.5f);
    glPushMatrix();
    glRotatef(outer_rotation,0,0,1);
    glColor3f(0,0,0);
    glCallList(body);
    glColor3f(1,1,1);
    glCallList(body);
    glPopMatrix();
    glRotatef(inner_rotation,0,0,1);
    glScalef(0.8,0.8,1);
    glCallList(body);
  }
  glPopMatrix();

  if (!minimap && !debris.empty()) {
    glPointSize(3.0f);
    glBegin(GL_POINTS);
    for (const auto& d : debris) {
      glColor4f(1.0f, 0.7f, 0.2f, d.aliveness());
      glVertex2fv(d.position);
    }
    glEnd();
  }
}

Save::Station GLStation::capture_state() const {
  Save::Station s;
  s.present = true;
  s.alive = alive;
  s.lives = lives;
  s.health = health;
  s.pos_x = position.x();
  s.pos_y = position.y();
  s.vel_x = velocity.x();
  s.vel_y = velocity.y();
  s.inner_rotation = inner_rotation;
  s.outer_rotation = outer_rotation;
  s.wave = wave;
  s.difficulty = difficulty;
  s.ships_this_wave = ships_this_wave;
  s.ships_left_to_deploy = ships_left_to_deploy;
  s.time_until_next_ship = time_until_next_ship;
  s.deploying = deploying;
  s.redeploying = redeploying;
  for (const auto *gs : *objects) {
    Save::Enemy e;
    e.pos_x = gs->ship->position.x();
    e.pos_y = gs->ship->position.y();
    e.vel_x = gs->ship->velocity.x();
    e.vel_y = gs->ship->velocity.y();
    e.facing_x = gs->ship->facing.x();
    e.facing_y = gs->ship->facing.y();
    e.thrust_force = gs->ship->thrust_force;
    e.rotation_force = gs->ship->rotation_force;
    e.value = gs->ship->value;
    s.enemies.push_back(e);
  }
  return s;
}

void GLStation::restore_state(const Save::Station &s, const Grid &grid) {
  alive = s.alive;
  lives = s.lives;
  health = s.health;
  position = WrappedPoint(s.pos_x, s.pos_y);
  velocity = Point(s.vel_x, s.vel_y);
  inner_rotation = s.inner_rotation;
  outer_rotation = s.outer_rotation;
  wave = s.wave;
  difficulty = s.difficulty;
  ships_this_wave = s.ships_this_wave;
  ships_left_to_deploy = s.ships_left_to_deploy;
  time_until_next_ship = (float)s.time_until_next_ship;
  deploying = s.deploying;
  redeploying = s.redeploying;
  for (const auto &se : s.enemies) {
    GLEnemy *ge = new GLEnemy(grid, se.pos_x, se.pos_y, targets, (float)difficulty, asteroids);
    ge->ship->alive = true;
    ge->ship->position = WrappedPoint(se.pos_x, se.pos_y);
    ge->ship->velocity = Point(se.vel_x, se.vel_y);
    ge->ship->facing = Point(se.facing_x, se.facing_y);
    ge->ship->thrust_force = se.thrust_force;
    ge->ship->rotation_force = se.rotation_force;
    ge->ship->value = se.value;
    objects->push_back(ge);
    // Skip the initial 2.5s lock delay — enemy is already deployed at saved position
    if (!ge->ship->behaviours.empty())
      if (Follower *f = dynamic_cast<Follower*>(ge->ship->behaviours.front()))
        f->lock_now();
  }
}

void GLStation::step(float delta, const Grid &grid) {
  Ship::step(delta, grid);
  if (!alive) return;
  outer_rotation += outer_rotation_speed * delta;
  inner_rotation += inner_rotation_speed * delta;
  if(deploying) {
    time_until_next_ship -= delta;
    if(ships_left_to_deploy == 0) {
      deploying = false;
      ships_this_wave += extra_ships_per_wave;
      if(ships_this_wave > max_ships_per_wave) {
        ships_this_wave = max_ships_per_wave;
        if(!redeploying) {
          difficulty++;
        } else {
          redeploying = false;
        }
      }
    } else if (time_until_next_ship <= 0) {
      time_until_next_ship += time_between_ships;
      ships_left_to_deploy--;
      float rotation = 360.0/ships_this_wave*ships_left_to_deploy*M_PI/180;
      float distance = 30 + radius;
      objects->push_back(
        new GLEnemy(
          grid,
          position.x() + distance*cos(rotation),
          position.y() + distance*sin(rotation), targets, difficulty, asteroids
        )
      );
    }
  } else if (objects->empty()) {
    deploying = true;
    wave++;
    time_until_next_ship = 0.0;
    ships_left_to_deploy = ships_this_wave;
  }
}
