#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"
#include "point.h"
#include "gltrail.h"
#include <vector>

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
  void step(float delta);
  virtual void input(unsigned char key, bool pressed = true);
  void set_keys(int left, int right, int up, int right);
  void draw();

  static void collide(GLShip* first, GLShip* second);
  Ship *ship;

protected:
  virtual void draw_ship();
  void draw_bullets();

  GLuint body, jets;
  
  float color[3];
  
  int thrust_key;
  int left_key;
  int right_key;
  int shoot_key;
  
  std::vector<GLTrail*> trails;
};

#endif
