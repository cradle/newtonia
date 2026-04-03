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

#include "gl_compat.h"
#include "mesh.h"

class GLTrail;

class GLShip {
public:
  GLShip(const Grid &grid, bool has_friction);
  virtual ~GLShip();
  void step(int delta, const Grid &grid);
  virtual void input(unsigned char key, bool pressed = true);
  virtual void controller_input(SDL_Event event);
  virtual void controller_axis_input(SDL_Event event);
  virtual void controller_touchpad_input(SDL_Event event);
  void touch_joystick_input(float nx, float ny);
  bool wasMyController(SDL_JoystickID id);

  void set_keys(int left, int right, int up, int down, int reverse, int mine, int next_weapon_key, int boost_key, int teleport_key, int help_key, int next_secondary_key);
  void set_controller(SDL_GameController *game_controller);
  bool has_controller() const;
  bool is_my_controller_id(SDL_JoystickID id) const;
  void genForceShield();
  void genRepulsor();
  void genGodShield();
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
  void snap_camera_to_heading();  // instantly align camera with ship heading (no interpolation)

  void collide_grid(Grid &grid, int delta);
  static void collide(GLShip* first, GLShip* second);
  Ship *ship;

  float color[3];

  friend class Overlay;

protected:
  virtual void draw_ship(bool minimap = false) const;
  void draw_particles() const;
  void draw_mines(bool minimap) const;
  void draw_giga_mines(bool minimap) const;
  void draw_shockwaves() const;
  void draw_missiles() const;
  void draw_debris() const;

  /*delegators*/
  float max_temperature() const;
  float temperature() const;
  float critical_temperature() const;
  float explode_temperature() const;

  // body_fill: black polygon; body_outline: ship-coloured line loop.
  Mesh body_fill, body_outline;
  Mesh jets, repulsors, force_shield;
  Mesh god_shield;     // yellow shield circle for god-mode invincibility
  Mesh minimap_dot;    // single white vertex at origin, tinted per draw
  Mesh missile_body;   // unit missile triangle (ship colour), per-missile matrix

  int thrust_key, left_key, right_key, shoot_key, reverse_key, mine_key, next_weapon_key, next_secondary_key, boost_key, teleport_key, help_key;

  SDL_GameController *controller = NULL;
  SDL_JoystickID controller_instance_id = -1;
  bool r2_shoot_active = false;
  bool l2_shoot_active = false;
  bool left_axis_x_active = false;
  bool left_axis_y_active = false;

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
