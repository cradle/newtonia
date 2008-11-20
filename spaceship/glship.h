#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"
#include "point.h"
#include <deque>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

class GLShip {
public:
  GLShip() {};
  GLShip(int x, int y);
  virtual ~GLShip();
  virtual void step(float delta);
  void resize(Point world_size);
  void input(unsigned char key, bool pressed = true);
  void set_keys(int left, int right, int up, int right);
  virtual void draw();

  static void collide(GLShip* first, GLShip* second);
  Ship *ship;

protected:
  Point world;

  int thrust_key;
  int left_key;
  int right_key;
  int shoot_key;
  
  std::deque<std::deque<Bullet*>*> trails;  
};

#endif
