#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"
#include "point.h"
#include "gltrail.h"
#include <vector>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

class GLShip {
public:
  GLShip() {};
  GLShip(int x, int y);
  virtual ~GLShip();
  void step(float delta);
  virtual void input(unsigned char key, bool pressed = true);
  void set_keys(int left, int right, int up, int right, int reverse, int mine);
  void draw(bool minimap = false);
  bool is_removable();

  static void collide(GLShip* first, GLShip* second);
  Ship *ship;

protected:
  virtual void draw_ship();
  void draw_bullets();
  void draw_mines();
  void draw_debris();

  GLuint body, jets, repulsors;
  
  float color[3];
  
  int thrust_key;
  int left_key;
  int right_key;
  int shoot_key;
  int reverse_key;
  int mine_key;
  
  std::vector<GLTrail*> trails;
};

#endif
