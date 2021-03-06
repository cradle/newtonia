#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"
#include "point.h"
#include "gltrail.h"
#include "typer.h"
#include <SDL.h>
#include <SDL_joystick.h>
#include <SDL_mixer.h>
#include <list>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

class GLTrail;

class GLShip {
public:
  GLShip(const Grid &grid, bool has_friction);
  virtual ~GLShip();
  void step(int delta, const Grid &grid);
  virtual void input(unsigned char key, bool pressed = true);
  virtual void controller_input(SDL_Event event);
  virtual void controller_axis_input(SDL_Event event);
  bool wasMyController(SDL_JoystickID id);

  void set_keys(int left, int right, int up, int down, int reverse, int mine, int next_weapon_key, int boost_key, int teleport_key, int help_key);
  void set_controller(SDL_GameController *game_controller);
  void genForceShield();
  void genRepulsor();
  void draw(bool minimap = false);
  void draw_body() const;
  void draw_keymap() const;
  void draw_temperature() const;
  void draw_respawn_timer() const;
  void draw_temperature_status() const;
  void draw_weapons() const;
  bool is_removable() const;
  //TODO: Clearly there is a Player/View/Controller separation here
  bool rotate_view() const;
  float camera_facing() const;
  float view_angle() const;

  void collide_grid(Grid &grid);
  static void collide(GLShip* first, GLShip* second);
  Ship *ship;

  float color[3];

  friend class Overlay;

protected:
  virtual void draw_ship(bool minimap = false) const;
  void draw_particles() const;
  void draw_mines(bool minimap) const;
  void draw_debris() const;

  /*delegators*/
  float max_temperature() const;
  float temperature() const;
  float critical_temperature() const;
  float explode_temperature() const;

  GLuint body, jets, repulsors, force_shield, force_shield_bg;

  int thrust_key, left_key, right_key, shoot_key, reverse_key, mine_key, next_weapon_key, boost_key, teleport_key, help_key;

  SDL_GameController *controller = NULL;

  bool rotating_view, show_help;
  float camera_rotation;
  float camera_angle;

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
inline
bool GLShip::rotate_view() const {
  return rotating_view;
}
inline
float GLShip::view_angle() const {
  return camera_angle;
}
#endif
