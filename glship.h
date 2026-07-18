#ifndef GL_SHIP_H
#define GL_SHIP_H

#include "ship.h"
#include "point.h"
#include "gltrail.h"
#include "preferences.h"
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
  void release_controls();
  bool wasMyController(SDL_JoystickID id);

  void set_keys(const PlayerKeys &k);
  // Strip every keyboard binding (the netplay ghost ship must never respond
  // to this machine's keys).
  void clear_keys();
  void set_keyboard_sensitivity(float s) { keyboard_sensitivity = s; }
  void set_camera_smoothing(float s)     { camera_smoothing = s; }
  // Per-player camera fixed/rotate: adopt the owning player's pref as the
  // initial state and remember where to persist an in-game toggle (the V
  // key / left-stick click). NULL for the remote ghost ship (no local input).
  void set_rotate_view_pref(bool *pref) {
    if (pref) rotating_view = *pref;
    rotate_view_pref_ = pref;
  }
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
  void snap_camera_to_heading();
  void smooth_camera(int frame_delta);

  // Netplay: input-intent mirrors sampled by the online client into INPUT
  // messages. The fire triggers must be tracked here (key intent) rather
  // than read from the weapon, because semi-automatic weapons clear their
  // own trigger after each shot. Meaningless offline. See NETPLAY.md.
  uint8_t net_respawn_count = 0;   // wrapping respawn-tap (shoot while dead)
  bool net_shoot_held = false;
  bool net_secondary_held = false;
  uint8_t net_shoot_press_count = 0;      // wrapping, one per key press
  uint8_t net_secondary_press_count = 0;

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
  void draw_shocks() const;
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

  // Two-slot bindings (primary + optional alternate; see KeyBinding).
  // Default-constructed empty, so a ship that never gets set_keys (the
  // netplay ghost) matches no keyboard input at all.
  KeyBinding thrust_key, left_key, right_key, shoot_key, reverse_key, mine_key, next_weapon_key, next_secondary_key, boost_key, teleport_key, help_key, toggle_rotate_view_key;
  float keyboard_sensitivity = 1.0f;  // rotation speed multiplier for keyboard input
  float camera_smoothing     = 0.004f; // camera follow rate (0 = instant snap)

  SDL_GameController *controller = NULL;
  SDL_JoystickID controller_instance_id = -1;
  bool r2_shoot_active = false;
  bool l2_shoot_active = false;
  bool left_axis_x_active = false;
  bool left_axis_y_active = false;
  bool kb_thrust = false;
  bool kb_reverse = false;
  bool kb_rotate_left = false;
  bool kb_rotate_right = false;

  bool rotating_view, show_help, last_input_was_controller;
  bool *rotate_view_pref_ = nullptr;  // per-player pref to persist on toggle
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
