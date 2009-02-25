#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"
#include "point.h"
#include "gltrail.h"
#include "typer.h"
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
  GLShip();
  virtual ~GLShip();
  void step(float delta);
  virtual void input(unsigned char key, bool pressed = true);
  void set_keys(int left, int right, int up, int right, int reverse, int mine);
  void draw(bool minimap = false);
  void draw_body() const;
  void draw_temperature() const;
  void draw_respawn_timer() const;
  void draw_temperature_status() const;
  bool is_removable() const;

  static void collide(GLShip* first, GLShip* second);
  Ship *ship;

protected:
  virtual void draw_ship(bool minimap = false) const;
  void draw_particles() const;
  void draw_mines() const;
  void draw_debris() const;
  
  /*delegators*/
  float max_temperature() const;
  float temperature() const;
  float critical_temperature() const;
  float explode_temperature() const;
  
  GLuint body, jets, repulsors, force_shield;
  
  float color[3];
  
  int thrust_key, left_key, right_key, shoot_key, reverse_key, mine_key;
  
  std::list<GLTrail*> trails;
};

/* delegators */
inline
float GLShip::max_temperature() const {
  return ship->max_temperature;
}
inline
float GLShip::temperature() const {
  return ship->temperature;
}
inline
float GLShip::critical_temperature() const {
  return ship->critical_temperature;
}
inline
float GLShip::explode_temperature() const {
  return ship->explode_temperature;
}

#endif
