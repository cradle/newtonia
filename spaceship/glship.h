#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"

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
  void resize(float screen_width, float screen_height);
  void input(unsigned char key, bool pressed = true);
  void set_keys(int left, int right, int up, int right);
  virtual void draw();

  static void collide(GLShip* first, GLShip* second);
  Ship *ship;

protected:
  float window_width, window_height;

  int thrust_key;
  int left_key;
  int right_key;
  int shoot_key;
  
  GLuint body_lines;
};

#endif
