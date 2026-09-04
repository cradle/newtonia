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
  // Draw one batch of bullet particles in a hull colour — the single home
  // of the streak/diamond vocabulary (world-bullet white, beam violet,
  // v23 bomber-shell diamond, slow-ordnance streak floor). Shared by
  // draw_particles and the net client's orphan enemy bullets (fire from a
  // dead, no-longer-replicated wave ship).
  static void draw_bullet_batch(const std::vector<Particle> &bullets,
                                const float col[3]);
  // tint: optional seat colour (FOURPLAYER.md D7) applied BEFORE the body
  // meshes bake; NULL keeps the classic P1 blue. The minimap dot and lives
  // icons read `color` live, so they follow automatically.
  GLShip(const Grid &grid, bool has_friction, const float *tint = NULL);
  virtual ~GLShip();
  void step(int delta, const Grid &grid);
  // Step only the exhaust trails. For a display-only hull (the Intro's
  // interceptor): the ship itself is never stepped — the frozen world must
  // stay frozen and its Follower must not run — but a thrusting pose needs
  // its plume to animate.
  void step_trails(int delta);
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
  // Which PlayerKeys slot this seat's bindings came from, or -1 for none (a
  // pad-only seat, or the netplay ghost). Seat index and keymap slot are
  // deliberately SEPARATE: the seat roster rebinds clusters across seats, so
  // "player 3 flies with WASD" is just seat 3 holding slot 0. Only
  // set_keys/clear_keys move it, so it can never disagree with the bindings.
  int keymap_slot() const { return keymap_slot_; }
  bool has_keys() const { return keymap_slot_ >= 0; }
  void set_keymap_slot(int slot) { keymap_slot_ = slot; }
  void set_keyboard_sensitivity(float s) { keyboard_sensitivity = s; }
  void set_camera_smoothing(float s)     { camera_smoothing = s; }
  // Per-player zoom prefs (Options CAMERA sub-menu), by pointer like the
  // rotate pref so menu changes apply to a live game. NULL (replay join
  // ghosts, intro display hulls, shot-harness and video-render ships)
  // means the classic view: base 1.0, no speed-follow. A netplay ghost
  // takes the VIEWER's slot-0 prefs (GLGame's set_viewer_zoom_prefs) —
  // spectating hands it the camera. The smoothed result is folded into
  // view_angle() by smooth_camera.
  void set_zoom_prefs(float *base, const float *follow) {
    zoom_base_pref_ = base;
    speed_zoom_pref_ = follow;
    // Snap to the base straight away — the rotation snap's twin: a game
    // start or CONTINUE opens AT the stored zoom instead of gliding there
    // from NORMAL over three time constants. The speed-follow part still
    // eases in from here (the hull's speed isn't restored yet when the
    // save ctor wires this).
    view_zoom = base ? *base : 1.0f;
  }
  // The in-game touch zoom zones (TouchZone::zoom_*): step the ZOOM pref
  // one Options step closer (dir < 0) or wider (dir > 0), clamped at the
  // ends and persisted like the rotate toggle; the eased view_zoom then
  // glides to the new base, which is the feedback. False when nothing
  // changed — no pref to step (ghosts, harness ships) or already at that
  // end (the ring still flashes, so the tap reads as answered).
  bool step_zoom(int dir);
  // For the overlay: the pref's current Options step — the classic step
  // with no pref, so a shot-harness ship still shows the zones a device
  // has (its taps stay inert) — and the tapped zone's brief pressed-look
  // flash (ms left, direction).
  int zoom_step_index() const;
  int zoom_flash_ms() const { return zoom_flash_ms_; }
  int zoom_flash_dir() const { return zoom_flash_dir_; }
  // Per-player camera fixed/rotate: adopt the owning player's pref as the
  // initial state and remember where to persist an in-game toggle (the V
  // key / left-stick click). NULL for the remote ghost ship (no local input).
  void set_rotate_view_pref(bool *pref) {
    if (pref) rotating_view = *pref;
    rotate_view_pref_ = pref;
  }
  void set_controller(SDL_GameController *game_controller);
  bool has_controller() const;
  // The seat's pad DISCONNECTED (as opposed to a deliberate
  // set_controller(NULL) from the roster): drop the binding and remember the
  // loss, so the next pad to appear comes back to this seat — a re-added pad
  // is a NEW SDL device (a USB pad re-paired wireless shares nothing with
  // the id that left), so recognising a reconnect can only mean remembering
  // which seat is waiting. See GLGame::controller_added.
  void controller_lost();
  bool awaiting_pad() const { return pad_lost_; }
  // A bound pad whose handle reports detached: its DEVICEREMOVED never
  // reached this seat (or the replacement's ADDED outran it) — the purge in
  // GLGame::controller_added treats it as the loss it is.
  bool controller_detached() const;
  // The keymap card is up, so it — not the pause text — owns the screen.
  // GLGame gates the pause menu on this (Overlay reads show_help directly
  // as a friend).
  bool showing_help() const { return show_help; }
  bool is_my_controller_id(SDL_JoystickID id) const;
  // Instance id of the bound pad, or -1 — the roster names pads by it.
  SDL_JoystickID controller_id() const { return controller_instance_id; }
  void genForceShield();
  void genRepulsor();
  void genGodShield();
  void draw(bool minimap = false);
  void draw_body() const;
  // fit uniformly scales the card; Overlay::keymap passes viewport-height /
  // card-span so the card fits a 2x2 grid cell instead of clipping its top.
  void draw_keymap(float fit = 1.0f) const;
  void draw_temperature() const;
  void draw_respawn_timer() const;
  void draw_temperature_status() const;
  void draw_weapons() const;
  bool is_removable() const;
  //TODO: Clearly there is a Player/View/Controller separation here
  bool rotate_view() const;
  float camera_facing() const;
  // The camera's effective vertical FOV in degrees: camera_angle with the
  // eased zoom scale folded in (tan-space, so the scale is a straight
  // multiplier on the visible span). Every consumer of the visible
  // rectangle — projection, cull, audio plateau, edge indicators — reads
  // this; the one deliberate exception is quantum observation, pinned to
  // the classic view in GLGame::is_point_faced_by_any_player.
  float view_angle() const;
  // Screenshot harness framing (`zoom`): base vertical FOV in degrees
  // (default 85 — smaller is closer). Gameplay never changes it; the
  // player zoom prefs scale OVER it (view_zoom stays 1.0 in the sandboxed
  // harness, so shot scripts frame exactly as before).
  void set_view_angle(float degrees) { camera_angle = degrees; }
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
  void draw_turrets(bool minimap) const;
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
  Mesh minimap_dot;    // unit white diamond at origin, tinted + scaled per draw
  Mesh missile_body;   // unit missile triangle (ship colour), per-missile matrix

  // Two-slot bindings (primary + optional alternate; see KeyBinding).
  // Default-constructed empty, so a ship that never gets set_keys (the
  // netplay ghost) matches no keyboard input at all.
  KeyBinding thrust_key, left_key, right_key, shoot_key, reverse_key, mine_key, next_weapon_key, next_secondary_key, boost_key, teleport_key, help_key, toggle_rotate_view_key;
  KeyBinding zoom_in_key, zoom_out_key;  // step the ZOOM pref (step_zoom)
  float keyboard_sensitivity = 1.0f;  // rotation speed multiplier for keyboard input
  float camera_smoothing     = 0.004f; // camera follow rate (0 = instant snap)

  int keymap_slot_ = -1;  // see keymap_slot()
  SDL_GameController *controller = NULL;
  SDL_JoystickID controller_instance_id = -1;
  bool pad_lost_ = false;  // see awaiting_pad()
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
  // Zoom prefs (see set_zoom_prefs) and the eased current zoom scale.
  // view_zoom chases base * speed-follow in smooth_camera on the same
  // simulated clock as the rotation smoothing; 1.0 = the classic view.
  float *zoom_base_pref_ = nullptr;         // writable: step_zoom persists through it
  const float *speed_zoom_pref_ = nullptr;
  float view_zoom = 1.0f;
  int zoom_flash_ms_ = 0;   // touch zoom zone pressed-look, sim ms left
  int zoom_flash_dir_ = 0;  // which zone: -1 "+", +1 "-"

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
#endif
