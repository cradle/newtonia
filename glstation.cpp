#include "glstation.h"
#include <math.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include "ship.h"
#include "glenemy.h"
#include <list>
#include <iostream>
// #include "follower.h"

using namespace std;

GLStation::GLStation(list<GLShip*>* objects, list<GLShip*>* targets) : Ship(false), objects(objects), targets(targets) {
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

void GLStation::reset() {
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

int GLStation::level() const {
  return wave;
}

void GLStation::draw(bool minimap) const {
  if(!alive)
    return;
  glPushMatrix();
  glTranslatef(position.x(), position.y(), 0);

  if(minimap) {
    glLineWidth(3.0f);
    glCallList(map_body);
  } else {
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
}

void GLStation::step(float delta, const Grid &grid) {
  Ship::step(delta, grid);
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
          position.x() + distance*cos(rotation),
          position.y() + distance*sin(rotation), targets, difficulty
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
