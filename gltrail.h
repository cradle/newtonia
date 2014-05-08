#ifndef GL_TRAIL_H
#define GL_TRAIL_H

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include "glship.h"
#include <list>

class GLShip;

class GLTrail {
public:
  GLTrail(GLShip* ship,
          float deviation = 0.05,
          Point offset = Point(),
          float speed = 0.25,
          float rotation = 0.0,
          int type = THRUSTING,
          float life = 250.0);
  virtual ~GLTrail();
  void draw();
  void step(float delta);
  void collide_grid(Grid &grid);

  //TODO: Would want constructor to take TYPE type = THRUSTING, but doesn't work
  enum TYPE {
    THRUSTING = 1,
    REVERSING = 2,
    LEFT = 4,
    RIGHT = 8,
    ALWAYS = 16
  };

private:
  void add();

  int type;
  GLShip* ship;
  Point offset;
  float deviation, rotation, speed, life;

  std::list<Particle*> trail;
};

#endif
