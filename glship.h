#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"
#include "point.h"
#include "gltrail.h"
#include <list>

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
  bool is_removable() const;

  static void collide(GLShip* first, GLShip* second);
  Ship *ship;

protected:
  virtual void draw_ship(bool minimap = false) const;
  void draw_particles() const;
  void draw_mines() const;
  void draw_debris() const;

  GLuint body, jets, repulsors;
  
  float color[3];
  
  int thrust_key, left_key, right_key, shoot_key, reverse_key, mine_key;
  
  std::list<GLTrail*> trails;
};

#endif
