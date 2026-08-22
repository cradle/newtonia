#include "glship.h"
#include "gltrail.h"
#include "ship.h"
#include "typer.h"
#include "teleport.h"
#include "weapon/base.h"
#include "weapon/god_mode.h"
#include "weapon/nova.h"
#include "weapon/beam.h"
#include "weapon/lance.h"
#include "mat4.h"
#include "preferences.h"
#include <math.h>
#include <SDL.h>

#include "gl_compat.h"
#include "mesh.h"

#include <list>
#include <iostream>
#include <string>
#include <cstdio>
#include <cctype>
#include <cstring>

using namespace std;

GLShip::GLShip(const Grid &grid, bool has_friction, const float *tint)
    : show_help(false), last_input_was_controller(false) {
  //TODO: load config from file (colours too)
  ship = new Ship(grid, has_friction);
  ship->player_ship = true;
  trails.push_back(new GLTrail(this, 0.01, Point(0,0), 0.3,0.0, GLTrail::THRUSTING, 2500.0));
  trails.push_back(new GLTrail(this, 0.5,Point(-4,17),-0.1, 0.9, GLTrail::REVERSING | GLTrail::RIGHT, 250.0));
  trails.push_back(new GLTrail(this, 0.5,Point( 4,17),-0.1,-0.9, GLTrail::REVERSING | GLTrail::LEFT, 250.0));

  rotating_view = g_prefs.rotate_view;
  camera_rotation = ship->heading();

  camera_angle = 85.0f;
  next_secondary_key = 0;
  toggle_rotate_view_key = 0;

  color[0] = 72/255.0;
  color[1] = 118/255.0;
  color[2] = 255/255.0;
  if (tint != NULL) {
    color[0] = tint[0];
    color[1] = tint[1];
    color[2] = tint[2];
  }

  {
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex( 0.0f, 1.0f); mb.vertex(-0.8f,-1.0f);
    mb.vertex( 0.0f,-0.5f); mb.vertex( 0.8f,-1.0f);
    mb.end();
    body_fill.upload(mb);

    mb.clear();
    mb.begin(GL_LINE_LOOP);
    mb.color(color[0], color[1], color[2]);
    mb.vertex( 0.0f, 1.0f); mb.vertex(-0.8f,-1.0f);
    mb.vertex( 0.0f,-0.5f); mb.vertex( 0.8f,-1.0f);
    mb.end();
    body_outline.upload(mb);
  }

  {
    float rc = 1.0f-color[0], gc = 1.0f-color[1], bc = 1.0f-color[2];
    MeshBuilder mb;
    mb.begin(GL_TRIANGLES);
    mb.color(rc, gc, bc);
    mb.vertex( 0.0f,-0.5f); mb.vertex(-0.4f,-0.75f); mb.vertex( 0.0f,-1.5f);
    mb.vertex( 0.0f,-0.5f); mb.vertex( 0.0f,-1.5f);  mb.vertex( 0.4f,-0.75f);
    mb.end();
    jets.upload(mb);
  }

  genForceShield();
  genRepulsor();
  genGodShield();

  {
    MeshBuilder mb;
    mb.begin(GL_POINTS);
    mb.color(1.0f, 1.0f, 1.0f);
    mb.vertex(0.0f, 0.0f);
    mb.end();
    minimap_dot.upload(mb);
  }

  {
    MeshBuilder mb;
    float sz = 5.0f;
    mb.begin(GL_LINE_LOOP);
    mb.color(color[0], color[1], color[2]);
    mb.vertex( 0.0f,  sz);
    mb.vertex(-sz * 0.5f, -sz);
    mb.vertex( sz * 0.5f, -sz);
    mb.end();
    missile_body.upload(mb);
  }
}

void GLShip::genGodShield() {
  const int segs = 20;
  const float shield_size = 2.0f;
  MeshBuilder mb;
  mb.begin(GL_LINE_LOOP);
  mb.color(1.0f, 1.0f, 0.0f, 1.0f);
  for (int i = 0; i < segs; i++) {
    float d = i * 2.0f * (float)M_PI / segs;
    mb.vertex(cosf(d) * shield_size, sinf(d) * shield_size);
  }
  mb.end();
  god_shield.upload(mb);
}

void GLShip::genRepulsor() {
  float rc = 1.0f-color[0], gc = 1.0f-color[1], bc = 1.0f-color[2];
  MeshBuilder mb;
  mb.begin(GL_TRIANGLES);
  mb.color(rc, gc, bc);
  mb.vertex( 0.3f,  0.3f); mb.vertex( 0.6f,  0.9f); mb.vertex( 0.9f,  0.9f);
  mb.vertex( 0.3f,  0.3f); mb.vertex( 0.9f,  0.9f); mb.vertex( 0.75f, 0.3f);
  mb.end();
  repulsors.upload(mb);
}

void GLShip::genForceShield() {
  const int number_of_segments = 20;
  const float segment_size = 360.0f / number_of_segments;
  const float shield_size  = 2.0f;

  MeshBuilder mb;
  mb.begin(GL_LINE_LOOP);
  mb.color(color[0], color[1], color[2], 1.0f);
  for (float i = 0.0f; i < 360.0f; i += segment_size) {
    float d = i * (float)M_PI / 180.0f;
    mb.vertex(cosf(d)*shield_size, sinf(d)*shield_size);
  }
  mb.end();
  force_shield.upload(mb);
}

GLShip::~GLShip() {
  delete ship;
  while(!trails.empty()) {
    delete trails.back();
    trails.pop_back();
  }
}

float GLShip::camera_facing() const {
  return -camera_rotation;
}

void GLShip::snap_camera_to_heading() {
  camera_rotation = ship->heading();
}

void GLShip::collide_grid(Grid &grid, int delta) {
    ship->collide_grid(grid, delta);
}

void GLShip::collide(GLShip* first, GLShip* second) {
  Ship::collide(first->ship, second->ship);
}

void GLShip::smooth_camera(int frame_delta) {
  float target = ship->heading();
  if (camera_smoothing == 0.0f) {
    camera_rotation = target;
    return;
  }
  float camera_rotation_delta = target - camera_rotation;
  while(camera_rotation_delta < -180)
    camera_rotation_delta += 360;
  while(camera_rotation_delta > 180)
    camera_rotation_delta -= 360;
  camera_rotation += camera_rotation_delta * frame_delta * camera_smoothing;
}

void GLShip::step(int delta, const Grid &grid) {
  ship->step(delta, grid);

  // e2e hook in the NEWTONIA_NET_TEST_* family (see test/e2e/weapons_net.sh):
  // keep this player stocked with the Pierce Beam and Lance so the drivers
  // can exercise them deterministically (drops are random). Granted once per
  // life — skipped while still present, so weapon cycling works normally.
  // Inert without the env var; replaces the old NEWTONIA_DEBUG_BEAM
  // compile-time cheat, so no special build is needed (or shipped).
  static const bool test_grant_weapons =
      SDL_getenv("NEWTONIA_NET_TEST_GRANT_WEAPONS") != NULL;
  // Never grant on a net CLIENT (net_quiet_respawn = the client game's
  // lifetime): weapons are host-owned state, so a client-side grant just
  // fights the 10 Hz snapshot restore — ammo pins at 999, selection snaps
  // back to the host's idea, and trigger pulls right after an apply fire
  // the default gun (Glenn's "different guns to the server"). Set the env
  // var on the HOST instead: it grants both ships and the weapons
  // replicate here through the ordinary snapshot path.
  if(test_grant_weapons && !Ship::net_quiet_respawn && ship->is_alive()) {
    bool has_beam = false, has_lance = false;
    for(Weapon::Base *w : ship->primary_weapons) {
      if(dynamic_cast<Weapon::Beam*>(w)) has_beam = true;
      else if(dynamic_cast<Weapon::Lance*>(w)) has_lance = true;
    }
    if(!has_lance) ship->add_lance_ammo(999);
    if(!has_beam) ship->add_beam_ammo(999);
  }

  // Turret twin (test/e2e/turret_net.sh), same host-side contract as above.
  // Deliberately its own hook rather than a row in GRANT_WEAPONS: with no
  // other secondary granted the turret is armed the moment it appears, and
  // the driver never has to walk the secondary cycle at a hostile
  // generation — the selection probes at generation 3 cost an idle joiner
  // all three lives before it ever reached TURRET (2026-08-17).
  static const bool test_grant_turrets =
      SDL_getenv("NEWTONIA_NET_TEST_GRANT_TURRETS") != NULL;
  if(test_grant_turrets && !Ship::net_quiet_respawn && ship->is_alive()) {
    bool has_turret = false;
    for(Weapon::Base *w : ship->secondary_weapons)
      if(dynamic_cast<Weapon::Turret*>(w)) { has_turret = true; break; }
    if(!has_turret) ship->add_turret_ammo(999);
  }

  step_trails(delta);
}

void GLShip::step_trails(int delta) {
  for(list<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
    (*i)->step(delta);
  }
}

void GLShip::set_keys(const PlayerKeys &k) {
  left_key = k.left;
  right_key = k.right;
  shoot_key = k.shoot;
  thrust_key = k.thrust;
  teleport_key = k.teleport;
  reverse_key = k.reverse;
  mine_key = k.mine;
  next_weapon_key = k.next_weapon;
  boost_key = k.boost;
  help_key = k.help;
  next_secondary_key = k.next_secondary;
  toggle_rotate_view_key = k.toggle_rotate_view;
}

void GLShip::clear_keys() {
  left_key = right_key = shoot_key = thrust_key = teleport_key = reverse_key =
  mine_key = next_weapon_key = boost_key = help_key = next_secondary_key =
  toggle_rotate_view_key = KeyBinding();
  keymap_slot_ = -1;
}

void GLShip::set_controller(SDL_GameController *game_controller) {
  controller = game_controller;
  if(controller) {
    controller_instance_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    last_input_was_controller = true;
  } else {
    controller_instance_id = -1;
  }
}

bool GLShip::has_controller() const {
  return controller_instance_id != -1;
}

bool GLShip::is_my_controller_id(SDL_JoystickID id) const {
  return id != -1 && controller_instance_id == id;
}

void GLShip::draw_temperature() const {
  if(ship->heat_rate <= 0.0f)
    return;

  float cr = ship->temperature_ratio();
  float cg = 1.0f - cr;

  // Apply y-scale(5) to the current VP so all sub-draws see it
  float base_vp[16]; gles2_get_mvp(base_vp);
  float scaled_vp[16]; mat4_scale(scaled_vp, base_vp, 1.0f, 5.0f, 1.0f);
  gles2_set_vp(scaled_vp);

  static MeshBuilder mb;
  static Mesh mesh;

  // Temperature bar fill (green → red)
  float temp = temperature() > critical_temperature() ? critical_temperature() : temperature();
  float th = temp / max_temperature();
  mb.clear();
  mb.begin(GL_TRIANGLES);
  mb.color(cr, cg, 0.0f);
  mb.vertex(0.0f, 0.0f); mb.vertex(1.0f, 0.0f); mb.vertex(1.0f, th);
  mb.vertex(0.0f, 0.0f); mb.vertex(1.0f, th);   mb.vertex(0.0f, th);
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();

  // Critical overflow fill (red)
  if(temperature() > critical_temperature()) {
    float cy = critical_temperature() / max_temperature();
    float oh = temperature() / max_temperature() - cy;
    float over_vp[16];
    mat4_translate(over_vp, scaled_vp, 0.0f, cy, 0.0f);
    mat4_scale(over_vp, over_vp, 1.0f, 0.5f, 1.0f);
    gles2_set_vp(over_vp);
    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(1.0f, 0.0f, 0.0f);
    mb.vertex(0.0f, 0.0f); mb.vertex(1.0f, 0.0f); mb.vertex(1.0f, oh);
    mb.vertex(0.0f, 0.0f); mb.vertex(1.0f, oh);   mb.vertex(0.0f, oh);
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
    gles2_set_vp(scaled_vp);
  }

  // Border
  mb.clear();
  mb.begin(GL_LINE_LOOP);
  mb.color(1.0f, 1.0f, 1.0f);
  mb.vertex(0.0f, 0.0f); mb.vertex(1.0f, 0.0f);
  mb.vertex(1.0f, 1.0f); mb.vertex(0.0f, 1.0f);
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();

  // Critical level line
  float crit_y = critical_temperature() / max_temperature();
  mb.clear();
  mb.begin(GL_LINES);
  mb.color(1.0f, 1.0f, 1.0f);
  mb.vertex(0.0f, crit_y); mb.vertex(1.0f, crit_y);
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();

  gles2_set_vp(base_vp);  // restore caller's VP
}

void GLShip::draw_respawn_timer() const {
  if(!ship->is_alive()) {
    if(ship->lives > 0) {
      if(ship->time_until_respawn < 3000) {
        Typer::draw(-1,1,ship->time_until_respawn/1000+1);
      } else if (ship->first_life) {
        Typer::draw(-5,1,"READY");
      }
    } else {
      Typer::draw_centered(0,4,"GameOver",2);
      Typer::draw_centered(0,-1,ship->score);
    }
  }
}

void GLShip::draw_temperature_status() const {
  if(ship->heat_rate <= 0.0f)
    return;
  if(temperature() > max_temperature()) {
    Typer::draw(0,0,"WARNING-TEMPERATURE CRITICAL");
  } else if(temperature() > critical_temperature()) {
    Typer::draw(0,0,"WARNING");
  }
}

bool GLShip::wasMyController(SDL_JoystickID id) {
  if(controller != NULL && SDL_GameControllerGetAttached(controller)) {
    if(id != SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
      return false;
    } else {
      return true;
    }
  } else {
    return false;
  }
}

void GLShip::controller_input(SDL_Event event) {
  if(!wasMyController(event.cbutton.which)) {
    return;
  }
  last_input_was_controller = true;
  bool pressed = event.cbutton.state == SDL_PRESSED;
  if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK && pressed) {
    show_help = !show_help;
  }
  if(!ship->is_alive()) {
    if(pressed && event.cbutton.button == SDL_CONTROLLER_BUTTON_A && ship->lives > 0 &&
       ship->time_until_respawn <= ship->respawn_time - 1000) {
      net_respawn_count++;
      ship->time_until_respawn = 0;
    }
    return;
  }
  if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
    ship->rotate_left(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
    ship->rotate_right(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
    ship->thrust(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
    ship->reverse(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
    net_shoot_held = pressed;
    if (pressed) net_shoot_press_count++;
    ship->shoot(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
    net_secondary_held = pressed;
    if (pressed) net_secondary_press_count++;
    ship->fire_secondary(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER && pressed) {
    ship->boost();
  } else if(event.cbutton.button == SDL_CONTROLLER_BUTTON_X && pressed) {
    ship->next_weapon();
  } else if(event.cbutton.button == SDL_CONTROLLER_BUTTON_Y && pressed) {
    ship->next_secondary_weapon();
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && pressed) {
    ship->net_teleport_count++;
    ship->behaviours.push_back(new Teleport(ship));
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK && pressed) {
    rotating_view = !rotating_view;
    if (rotate_view_pref_) *rotate_view_pref_ = rotating_view;
    else g_prefs.rotate_view = rotating_view;
    save_preferences();
  }
}

void GLShip::controller_axis_input(SDL_Event event) {
  if(!wasMyController(event.cbutton.which)) {
    return;
  }
  if(!ship->is_alive()) {
    if(event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
      bool pressed = event.caxis.value > 8000;
      if(pressed && !r2_shoot_active && ship->lives > 0 &&
         ship->time_until_respawn <= ship->respawn_time - 1000) {
        net_respawn_count++;
      ship->time_until_respawn = 0;
      }
      r2_shoot_active = pressed;
    }
    return;
  }
  Sint16 deadzone = 10000;

  if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
    float scale = (std::abs((float)event.caxis.value) - deadzone) / (float)(32767 - deadzone);
    if(scale < 0.0f) scale = 0.0f;
    if(event.caxis.value > deadzone) {
      last_input_was_controller = true;
      left_axis_x_active = true;
      ship->rotation_scale = scale;
      ship->rotate_right(true);
      ship->rotate_left(false);
    } else if (event.caxis.value < -deadzone) {
      last_input_was_controller = true;
      left_axis_x_active = true;
      ship->rotation_scale = scale;
      ship->rotate_left(true);
      ship->rotate_right(false);
    } else {
      if (left_axis_x_active) {
        ship->rotation_scale = 1.0f;
        if(!kb_rotate_left)  ship->rotate_left(false);
        if(!kb_rotate_right) ship->rotate_right(false);
      }
      left_axis_x_active = false;
    }
  } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
    float scale = (std::abs((float)event.caxis.value) - deadzone) / (float)(32767 - deadzone);
    if(scale < 0.0f) scale = 0.0f;
    if(event.caxis.value > deadzone) {
      last_input_was_controller = true;
      left_axis_y_active = true;
      ship->reverse_analog = scale;
      ship->thrust_analog  = 1.0f;
      ship->reverse(true);
      ship->thrust(false);
    } else if (event.caxis.value < -deadzone) {
      last_input_was_controller = true;
      left_axis_y_active = true;
      ship->thrust_analog  = scale;
      ship->reverse_analog = 1.0f;
      ship->thrust(true);
      ship->reverse(false);
    } else {
      if (left_axis_y_active) {
        ship->thrust_analog  = 1.0f;
        ship->reverse_analog = 1.0f;
        if(!kb_thrust)   ship->thrust(false);
        if(!kb_reverse)  ship->reverse(false);
      }
      left_axis_y_active = false;
    }
  } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
    bool pressed = event.caxis.value > 8000;
    if(pressed != r2_shoot_active) {
      if(pressed) last_input_was_controller = true;
      r2_shoot_active = pressed;
      net_shoot_held = pressed;
    if (pressed) net_shoot_press_count++;
    ship->shoot(pressed);
    }
  } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
    bool pressed = event.caxis.value > 8000;
    if(pressed != l2_shoot_active) {
      if(pressed) last_input_was_controller = true;
      l2_shoot_active = pressed;
      net_secondary_held = pressed;
    if (pressed) net_secondary_press_count++;
    ship->fire_secondary(pressed);
    }
  }
}

void GLShip::touch_joystick_input(float nx, float ny) {
  if(!ship->is_alive()) return;
  const float dz = 0.10f;

  // Rotation (x axis): positive nx = right on screen = rotate right
  float abs_nx = nx < 0.0f ? -nx : nx;
  if(abs_nx > dz) {
    ship->rotation_scale = abs_nx;
    if(nx > 0.0f) {
      ship->rotate_right(true);
      ship->rotate_left(false);
    } else {
      ship->rotate_left(true);
      ship->rotate_right(false);
    }
  } else {
    ship->rotation_scale = 1.0f;
    ship->rotate_left(false);
    ship->rotate_right(false);
  }

  // Thrust / reverse (y axis): SDL y increases downward,
  // so ny < 0 = joystick pushed up = thrust forward.
  float abs_ny = ny < 0.0f ? -ny : ny;
  if(ny < -dz) {
    ship->thrust_analog  = abs_ny;
    ship->reverse_analog = 1.0f;
    ship->thrust(true);
    ship->reverse(false);
  } else if(ny > dz) {
    ship->reverse_analog = abs_ny;
    ship->thrust_analog  = 1.0f;
    ship->reverse(true);
    ship->thrust(false);
  } else {
    ship->thrust_analog  = 1.0f;
    ship->reverse_analog = 1.0f;
    ship->thrust(false);
    ship->reverse(false);
  }
}

void GLShip::controller_touchpad_input(SDL_Event event) {
  // Only handle the left touchpad (index 0) on Steam Deck
  if(event.ctouchpad.touchpad != 0) return;
  if(!wasMyController(event.ctouchpad.which)) return;

  if(event.type == SDL_CONTROLLERTOUCHPADUP) {
    touch_joystick_input(0.0f, 0.0f);
    return;
  }

  // Convert touchpad [0,1] coordinates to joystick [-1,1] centred at 0.5
  float nx = (event.ctouchpad.x - 0.5f) * 2.0f;
  float ny = (event.ctouchpad.y - 0.5f) * 2.0f;
  touch_joystick_input(nx, ny);
}

// Force-release every held control. Called whenever a state stops routing
// input to the ships (pause, the between-level intro): a key/stick release
// delivered while nobody was listening leaves flags like Ship::thrusting
// latched on, and a centred stick generates no further SDL events to clear
// them — the ship flies off on its own until the player re-taps the control.
void GLShip::release_controls() {
  kb_thrust = kb_reverse = kb_rotate_left = kb_rotate_right = false;
  left_axis_x_active = left_axis_y_active = false;
  r2_shoot_active = l2_shoot_active = false;
  ship->rotation_scale = 1.0f;
  ship->thrust_analog  = 1.0f;
  ship->reverse_analog = 1.0f;
  ship->thrust(false);
  ship->reverse(false);
  ship->rotate_left(false);
  ship->rotate_right(false);
  ship->shoot(false);
  ship->fire_secondary(false);
}

void GLShip::input(unsigned char key, bool pressed) {
  if (help_key.matches(key) && pressed) show_help = !show_help;
  if(!ship->is_alive()) {
    kb_thrust = kb_reverse = kb_rotate_left = kb_rotate_right = false;
    if(shoot_key.matches(key) && ship->lives > 0 &&
       ship->time_until_respawn <= ship->respawn_time - 1000) {
      last_input_was_controller = false;
      net_respawn_count++;
      ship->time_until_respawn = 0;
    }
    return;
  }
  if (left_key.matches(key) || right_key.matches(key) || thrust_key.matches(key) ||
      reverse_key.matches(key) || shoot_key.matches(key) || mine_key.matches(key) ||
      boost_key.matches(key) || next_weapon_key.matches(key) ||
      next_secondary_key.matches(key) || teleport_key.matches(key) ||
      help_key.matches(key) || toggle_rotate_view_key.matches(key)) {
    last_input_was_controller = false;
  }
  if (left_key.matches(key)) {
    if (pressed) ship->rotation_scale = keyboard_sensitivity;
    kb_rotate_left = pressed;
    ship->rotate_left(pressed);
  } else if (right_key.matches(key)) {
    if (pressed) ship->rotation_scale = keyboard_sensitivity;
    kb_rotate_right = pressed;
    ship->rotate_right(pressed);
  } else if (thrust_key.matches(key)) {
    kb_thrust = pressed;
    ship->thrust(pressed);
  } else if (reverse_key.matches(key)) {
    kb_reverse = pressed;
    ship->reverse(pressed);
  } else if (shoot_key.matches(key)) {
    net_shoot_held = pressed;
    if (pressed) net_shoot_press_count++;
    ship->shoot(pressed);
  } else if (mine_key.matches(key)) {
    net_secondary_held = pressed;
    if (pressed) net_secondary_press_count++;
    ship->fire_secondary(pressed);
  } else if (boost_key.matches(key) && pressed) {
    ship->boost();
  } else if(next_weapon_key.matches(key) && pressed) {
    ship->next_weapon();
  } else if(next_secondary_key.matches(key) && pressed) {
    ship->next_secondary_weapon();
  } else if (teleport_key.matches(key) && pressed) {
    ship->net_teleport_count++;
    ship->behaviours.push_back(new Teleport(ship));
  } else if (toggle_rotate_view_key.matches(key) && pressed) {
    rotating_view = !rotating_view;
    if (rotate_view_pref_) *rotate_view_pref_ = rotating_view;
    else g_prefs.rotate_view = rotating_view;
    save_preferences();
  }
}

void GLShip::draw(bool minimap) {
  if(!minimap) {
    draw_particles();
    draw_debris();
    list<GLTrail*>::iterator i;
    for(i = trails.begin(); i != trails.end(); i++) {
      (*i)->draw();
    }
  }
  draw_mines(minimap);
  draw_giga_mines(minimap);
  draw_turrets(minimap);
  if(!minimap) {
    draw_missiles();
    draw_shockwaves();
    draw_shocks();
  }
  if(ship->is_alive()) {
    draw_ship(minimap);
  }
}

void GLShip::draw_ship(bool minimap) const {
  // Build ship model: translate(pos) × scale(radius) × rotate_z(heading)
  float tile_vp[16]; gles2_get_mvp(tile_vp);
  float ship_model[16]; mat4_identity(ship_model);
  mat4_translate(ship_model, ship_model, ship->position.x(), ship->position.y(), 0.0f);
  mat4_scale(ship_model, ship_model, ship->radius, ship->radius, 1.0f);
  mat4_rotate_z(ship_model, ship_model, ship->heading());
  float ship_mvp[16]; mat4_mul(ship_mvp, tile_vp, ship_model);
  gles2_set_vp(ship_mvp);

  if(minimap) {
    minimap_dot.draw_tinted(color[0], color[1], color[2], 1.0f, 5.0f);
    gles2_set_vp(tile_vp);
    return;
  }

  glLineWidth(1.8f);

  if(ship->thrusting) {
    jets.draw();
  }

  if(ship->reversing) {
    repulsors.draw();
    float flip_mvp[16]; mat4_rotate_y(flip_mvp, ship_mvp, 180.0f);
    gles2_set_vp(flip_mvp);
    repulsors.draw();
    gles2_set_vp(ship_mvp);
  }

  draw_body();

  if(ship->invincible) {
    if(ship->god_mode_time_remaining() > 0) {
      god_shield.draw();
    } else {
      force_shield.draw();
    }
  }

  gles2_set_vp(tile_vp);
}

void GLShip::draw_body() const {
  body_fill.draw();
  body_outline.draw();
}

// Convert an internal game key code to a short display label.
// F-keys are encoded as 128 + GLUT_KEY_Fn (129=F1, 136=F8, etc.).
static std::string key_label(int key) {
  // The two labels that diverge from the INI's canonical names (which the
  // keymap would show too long): ESC not ESCAPE, ENTER not RETURN.
  if (key == 27)   return "ESC";
  if (key == 13)   return "ENTER";
  // Everything else named: the canonical table (preferences.cpp),
  // uppercased for the HUD.
  if (const char *n = special_key_name(key)) {
    std::string s(n);
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)::toupper(s[i]);
    return s;
  }
  if (key >= 129 && key <= 140) {
    char buf[8];
    snprintf(buf, sizeof(buf), "F%d", key - 128);
    return buf;
  }
  if (key >= 33 && key < 127)
    return std::string(1, (char)::toupper(key));
  char buf[16];
  snprintf(buf, sizeof(buf), "[%d]", key);
  return buf;
}

// Label a full binding: primary, plus "/ALT" when an alternate is bound
// (e.g. "W/UP" for thrust with its arrow alias).
static std::string binding_label(const KeyBinding &b) {
  std::string s = key_label(b.keys[0]);
  if (b.keys[1] != 0) s += "/" + key_label(b.keys[1]);
  return s;
}

void GLShip::draw_keymap(float fit) const {
  // fit scales the whole card uniformly (Overlay::keymap computes it from
  // the viewport height): laid out for a full-height viewport, the card
  // spans ~+485..-250 virtual units and clipped its top half off a 2x2
  // grid cell (4P field bug — the quarter showed the list from MINE down).
  float size = 10 * fit;
  int num_controls  = 10;
  if(last_input_was_controller) {
    num_controls++;
  }
  float padding = 2.0f * fit;
  float char_height = 5.0f;
  float y_offset = (last_input_was_controller ? 110.0f : 140.0f) * fit;
  Typer::draw_centered(0, (num_controls+1.5)/2.0f * (size + padding) * char_height + y_offset, "- PLAYER -", size+2);
  float offset = -160.0f * fit;
  int control_index = 0;

  // Draw a controller button: circled glyph for single-char buttons (A/B/X/Y/…),
  // L3/R3 labels for stick clicks, plain text for everything else.
  auto draw_btn = [&](float x, float y, SDL_GameControllerButton btn) {
    if (btn == SDL_CONTROLLER_BUTTON_LEFTSTICK)
      Typer::draw(x, y, "Left Stick Button", size);
    else if (btn == SDL_CONTROLLER_BUTTON_RIGHTSTICK)
      Typer::draw(x, y, "Right Stick Button", size);
    else {
      const char *s = SDL_GameControllerGetStringForButton(btn);
      if (strlen(s) == 1)
        Typer::draw_button(x, y, s[0], size);
      else
        Typer::draw(x, y, s, size);
    }
  };

  if(last_input_was_controller) {
    Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "MOVE", size);
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "LEFT STICK", size);
    control_index++;
  }
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "THRUST", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(thrust_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_DPAD_UP);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "REVERSE", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(reverse_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TURN RIGHT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(right_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TURN LEFT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(left_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "SHOOT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(shoot_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_A);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "MINE", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(mine_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_B);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "CHANGE WEAPON", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(next_weapon_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_X);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "CHANGE SECONDARY", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(next_secondary_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_Y);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "BOOST", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(boost_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TELEPORT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(teleport_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "ROTATE VIEW", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, binding_label(toggle_rotate_view_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_LEFTSTICK);
  }
  control_index++;

  int common_offset = control_index+1;
  const GeneralKeys &gk = g_prefs.general_keys;
  // Rows advance in steps of 1.0 below common_offset; keyboard-only rows
  // (fullscreen, friendly fire, cheats) are skipped on controller so the
  // list stays gap-free.
  float row = common_offset + 1.5f;
  auto row_y = [&]() { return (num_controls-row)/2.0f * (size + padding) * char_height + y_offset; };
  Typer::draw_centered(0, (num_controls-common_offset )/2.0f * (size + padding) * char_height + y_offset, "- GAME -", size +2);
  Typer::draw(offset, row_y(), "PAUSE", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, row_y(), key_label(gk.pause).c_str(), size);
  } else {
    draw_btn(-offset, row_y(), SDL_CONTROLLER_BUTTON_START);
  }
  row += 1.0f;
  if(!last_input_was_controller) {
    Typer::draw(offset, row_y(), "FULLSCREEN", size);
    Typer::draw(-offset, row_y(), key_label(gk.toggle_fullscreen).c_str(), size);
    row += 1.0f;
    Typer::draw(offset, row_y(), "FRIENDLY FIRE", size);
    Typer::draw(-offset, row_y(), key_label(gk.toggle_friendly_fire).c_str(), size);
    row += 1.0f;
  }
  Typer::draw(offset, row_y(), "HIDE THIS", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, row_y(), binding_label(help_key).c_str(), size);
  } else {
    draw_btn(-offset, row_y(), SDL_CONTROLLER_BUTTON_RIGHTSTICK);
  }
  row += 1.0f;
  Typer::draw(offset, row_y(), "QUIT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, row_y(), key_label(gk.menu).c_str(), size);
  } else {
    draw_btn(-offset, row_y(), SDL_CONTROLLER_BUTTON_BACK);
  }
  row += 1.0f;

  // Cheats are keyboard-only (see GLGame::keyboard_up) — hide on controller.
  if(!last_input_was_controller) {
    row += 1.0f;
    Typer::draw_centered(0, row_y(), "- CHEATS -", size +2);
    row += 1.5f;
    Typer::draw(offset, row_y(), "SPEED UP", size);
    Typer::draw(-offset, row_y(), key_label(gk.time_speed_up).c_str(), size);
    row += 1.0f;
    Typer::draw(offset, row_y(), "SLOW DOWN", size);
    Typer::draw(-offset, row_y(), key_label(gk.time_slow_down).c_str(), size);
    row += 1.0f;
    Typer::draw(offset, row_y(), "RESET SPEED", size);
    Typer::draw(-offset, row_y(), key_label(gk.time_reset).c_str(), size);
    row += 1.0f;
    Typer::draw(offset, row_y(), "SKIP LEVEL", size);
    Typer::draw(-offset, row_y(), key_label(gk.skip_level).c_str(), size);
  }
}

void GLShip::draw_weapons() const {
  const int size = 10;
  const int cw = size * 2;  // character width at this size

  // Returns the display string for a keyboard key.
  auto key_str = [](int key, char buf[8]) -> const char * {
    if (key == ' ') { buf[0]='S'; buf[1]='P'; buf[2]='C'; buf[3]='\0'; }
    else            { buf[0]=(char)key; buf[1]='\0'; }
    return buf;
  };

  // Draw one weapon row:
  //   NAME  [ammo]
  //   FIRE [key]      NEXT [key]
  auto draw_weapon_row = [&](int row_y, Weapon::Base *weapon, bool has_next,
                             bool flash_low,
                             const KeyBinding &cycle_key_bind, SDL_GameControllerButton cycle_btn,
                             const KeyBinding &fire_key_bind,  SDL_GameControllerButton fire_btn) {
    // The 3-char HUD slot only fits one key, so show the binding's primary.
    int cycle_key_kb = cycle_key_bind.primary();
    int fire_key_kb  = fire_key_bind.primary();
    // Low-ammo warning: flash the whole row title — name and count — at
    // 2 Hz over the last LOW_AMMO_WARN rounds so a gun running dry is
    // visible before the dry-switch takes it away. Primaries only
    // (flash_low): secondaries are deliberate single shots with visible
    // counts, and a flashing mine row read as an alarm. GodMode is
    // excluded — its "ammo" is remaining milliseconds and it has its own
    // indicator.
    bool low = flash_low &&
               !weapon->is_unlimited() && weapon->ammo() > 0 &&
               weapon->ammo() <= Weapon::Base::LOW_AMMO_WARN &&
               !dynamic_cast<Weapon::GodMode*>(weapon);
    bool blink_hidden = low && (SDL_GetTicks() / 250) % 2 != 0;

    // Line 1: NAME  ammo
    int cx = 10;
    if (!blink_hidden)
      Typer::draw(cx, row_y, weapon->name(), size);
    cx += (int)strlen(weapon->name()) * cw + 2 * cw;  // name + gap

    if (!weapon->is_unlimited()) {
      if (weapon->ammo() == 0) {
        Typer::draw(cx, row_y, "empty", size);
      } else if (!blink_hidden) {
        int display_ammo = dynamic_cast<Weapon::GodMode*>(weapon) ? weapon->ammo()/1000 : weapon->ammo();
        Typer::draw_lefted(cx + 2*cw, row_y, display_ammo, size);
      }
    }

    // Line 2: FIRE [key]   NEXT [key]  — keyboard/controller only, not touch
    if (!is_touch_mode()) {
      // Fixed columns so both rows always line up:
      //   col 0  = 10        "FIRE "   (5 chars)
      //   col 1  = 110       fire key  (up to 3 chars, e.g. "SPC")
      //   col 2  = 190       "NEXT "   (5 chars)
      //   col 3  = 290       cycle key
      const int col_fire      = 10;
      const int col_fire_key  = col_fire + 5 * cw;   // 110
      const int col_next      = col_fire_key + 4 * cw; // 190  (4-char slot for key)
      const int col_next_key  = col_next + 5 * cw;   // 290
      int bind_y = row_y - 35;
      char buf[8];

      Typer::draw(col_fire, bind_y, "FIRE ", size);
      if (last_input_was_controller) {
        Typer::draw_button(col_fire_key, bind_y, SDL_GameControllerGetStringForButton(fire_btn)[0], size);
      } else {
        Typer::draw(col_fire_key, bind_y, key_str(fire_key_kb, buf), size);
      }

      if (has_next) {
        Typer::draw(col_next, bind_y, "NEXT ", size);
        if (last_input_was_controller) {
          Typer::draw_button(col_next_key, bind_y, SDL_GameControllerGetStringForButton(cycle_btn)[0], size);
        } else {
          Typer::draw(col_next_key, bind_y, key_str(cycle_key_kb, buf), size);
        }
      }
    }
  };

  int y = -20;
  Typer::draw(10, y, "Weapons", 15);
  y -= 55;

  if (!ship->primary_weapons.empty()) {
    Weapon::Base *weapon = *(ship->primary);
    if (weapon != NULL) {
      draw_weapon_row(y, weapon, ship->primary_weapons.size() > 1,
        /*flash_low=*/true,
        next_weapon_key,   SDL_CONTROLLER_BUTTON_X,
        shoot_key,         SDL_CONTROLLER_BUTTON_A);
    }
  }

  if (!ship->secondary_weapons.empty()) {
    Weapon::Base *weapon = *(ship->secondary);
    // Hide Nova from the cycling slot when there's no bomb ready
    Weapon::Nova *nova_w = dynamic_cast<Weapon::Nova*>(weapon);
    if (weapon != NULL && (!nova_w || nova_w->ammo() > 0)) {
      draw_weapon_row(y - (is_touch_mode() ? 45 : 80), weapon, ship->secondary_weapons.size() > 1,
        /*flash_low=*/false,
        next_secondary_key, SDL_CONTROLLER_BUTTON_Y,
        mine_key,           SDL_CONTROLLER_BUTTON_B);
    }
  }

  // Nova charge counter: shows charge progress toward next bomb
  if(ship->nova_charge > 0) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/10", ship->nova_charge);
    int nova_y = y - (is_touch_mode() ? 90 : 160);
    Typer::draw(10,  nova_y, "NOVA SHARD", 10);
    Typer::draw(230, nova_y, buf,           10);
  }
}

void GLShip::draw_particles() const {
  static MeshBuilder mb;
  static Mesh mesh;

  //TODO: ParticleDrawer::draw(ship->bullets);
  if (!ship->bullets.empty()) {
    mb.clear();
    mb.begin(GL_LINES);
    for(auto b = ship->bullets.begin(); b != ship->bullets.end(); b++) {
      //TODO: Work out how to make bullets draw themselves. GLBullet?
      if(b->world_bullet) {
        mb.color(1.0f, 1.0f, 1.0f);
      } else if(b->piercing) {
        mb.color(0.7f, 0.4f, 1.0f);   // beam lance: violet, matching the pickup
      } else {
        mb.color(color[0], color[1], color[2]);
      }
      // Beam bolts draw a longer streak to read as a lance.
      Point tail = b->position - b->velocity * (b->piercing ? 22 : 10);
      mb.vertex(tail.x(), tail.y());
      mb.vertex(b->position.x(), b->position.y());
    }
    mb.end();
    glLineWidth(2.5f);
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }

  if(!ship->lance_pulses.empty()) {
    mb.clear();
    mb.begin(GL_LINES);
    for(auto &p : ship->lance_pulses) {
      float a = p.aliveness();
      // Amber flash matching the pickup, fading out over the pulse's ttl.
      mb.color(1.0f, 0.85f, 0.35f, a);
      for(size_t k = 0; k + 1 < p.points.size(); k++) {
        mb.vertex(p.points[k].x(), p.points[k].y());
        mb.vertex(p.points[k + 1].x(), p.points[k + 1].y());
      }
    }
    mb.end();
    glLineWidth(3.5f);
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
    glLineWidth(2.5f);
  }

  if(!ship->bullet_trails.empty()) {
    mb.clear();
    mb.begin(GL_POINTS);
    for(auto &p : ship->bullet_trails) {
      float a = p.aliveness();
      mb.color(a, a, 0.0f, a);
      mb.vertex(p.position.x(), p.position.y());
    }
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw(2.5f);
  }
}

bool GLShip::is_removable() const {
  return ship->is_removable();
}

void GLShip::draw_debris() const {
  if(ship->debris.empty()) return;

  static float flicker[64];
  static bool initialized = false;
  static int idx = 0;
  if(!initialized) {
    for(int i = 0; i < 64; i++)
      flicker[i] = rand() / (2.0f * (float)RAND_MAX) + 0.5f;
    initialized = true;
  }

  static MeshBuilder mb;
  static Mesh mesh;

  bool any_points = false, any_streaks = false;
  mb.clear();
  mb.begin(GL_POINTS);
  for(auto d = ship->debris.begin(); d != ship->debris.end(); d++) {
    if(d->streak) { any_streaks = true; continue; }
    mb.color(color[0], flicker[idx++ % 64], color[2], d->aliveness());
    mb.vertex(d->position.x(), d->position.y());
    any_points = true;
  }
  mb.end();
  if(any_points) {
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw(2.5f);
  }

  // Streak-flagged debris (the peer's mine blast on a net client) draws
  // exactly like draw_particles' bullets: a solid tail-to-position line.
  if(any_streaks) {
    mb.clear();
    mb.begin(GL_LINES);
    for(auto d = ship->debris.begin(); d != ship->debris.end(); d++) {
      if(!d->streak) continue;
      mb.color(color[0], color[1], color[2]);
      Point tail = d->position - d->velocity * 10;
      mb.vertex(tail.x(), tail.y());
      mb.vertex(d->position.x(), d->position.y());
    }
    mb.end();
    glLineWidth(2.5f);
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }
}

void GLShip::draw_mines(bool minimap) const {
  if(ship->mines.empty()) return;

  static MeshBuilder mb;
  static Mesh mesh;

  if(minimap) {
    mb.clear();
    mb.begin(GL_POINTS);
    mb.color(color[0], color[1], color[2]);
    for(auto m = ship->mines.begin(); m != ship->mines.end(); m++) {
      mb.vertex(m->position.x(), m->position.y());
    }
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw(1.5f);
    return;
  }

  // Mine cross (rotated diamond)
  float size = 7.5f;
  mb.clear();
  mb.begin(GL_LINES);
  mb.color(color[0], color[1], color[2]);
  for(auto m = ship->mines.begin(); m != ship->mines.end(); m++) {
    float angle = m->rotation * (float)M_PI / 180.0f;
    Point v0(0,-size), v1(size,0), v2(0,size), v3(-size,0);
    v0.rotate(angle); v1.rotate(angle); v2.rotate(angle); v3.rotate(angle);
    v0 += m->position; v1 += m->position; v2 += m->position; v3 += m->position;
    mb.vertex(v0.x(), v0.y()); mb.vertex(v1.x(), v1.y());
    mb.vertex(v1.x(), v1.y()); mb.vertex(v2.x(), v2.y());
    mb.vertex(v2.x(), v2.y()); mb.vertex(v3.x(), v3.y());
    mb.vertex(v3.x(), v3.y()); mb.vertex(v0.x(), v0.y());
  }
  mb.end();
  glLineWidth(2.0f);
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();

  // Pulsing red circles: one GL_LINE_LOOP group per mine, all in one VBO
  float t = (SDL_GetTicks() % 1000) / 1000.0f;
  float pulse = 0.5f + 0.5f * sinf(t * 2.0f * (float)M_PI);
  float pulse_radius = size + 4.5f;
  mb.clear();
  for(auto m = ship->mines.begin(); m != ship->mines.end(); m++) {
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 0.0f, 0.0f, pulse);
    for(int i = 0; i < 16; i++) {
      float a = i * 2.0f * (float)M_PI / 16.0f;
      mb.vertex(cosf(a) * pulse_radius + m->position.x(),
                sinf(a) * pulse_radius + m->position.y());
    }
    mb.end();
  }
  glLineWidth(1.5f);
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

void GLShip::draw_turrets(bool minimap) const {
  if(ship->turrets.empty()) return;

  static MeshBuilder mb;
  static Mesh mesh;

  if(minimap) {
    mb.clear();
    mb.begin(GL_POINTS);
    mb.color(color[0], color[1], color[2]);
    for(auto &t : ship->turrets)
      mb.vertex(t.position.x(), t.position.y());
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw(2.5f);
    return;
  }

  // Body: an owner-coloured circle with a hub dot; the last 5 seconds (or
  // last 5 rounds) dim the ring so retirement doesn't come out of nowhere.
  mb.clear();
  for(auto &t : ship->turrets) {
    float fade = 1.0f;
    if(t.ms_left < 5000.0f || t.shots_left <= 5) fade = 0.45f;
    mb.begin(GL_LINE_LOOP);
    mb.color(color[0], color[1], color[2], fade);
    for(int i = 0; i < 18; i++) {
      float a = i * 2.0f * (float)M_PI / 18.0f;
      mb.vertex(cosf(a) * TurretDrone::RADIUS + t.position.x(),
                sinf(a) * TurretDrone::RADIUS + t.position.y());
    }
    mb.end();
  }
  glLineWidth(2.0f);
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();

  // Barrel: a thick stub from the hub out past the ring, pointing where the
  // turret aims — full brightness while tracking a target, dimmed on the
  // idle sweep so "armed and hunting" reads at a glance.
  mb.clear();
  mb.begin(GL_LINES);
  for(auto &t : ship->turrets) {
    mb.color(color[0], color[1], color[2], t.has_target ? 1.0f : 0.5f);
    float ca = cosf(t.aim), sa = sinf(t.aim);
    mb.vertex(t.position.x() + ca * 3.0f, t.position.y() + sa * 3.0f);
    mb.vertex(t.position.x() + ca * TurretDrone::BARREL_LEN,
              t.position.y() + sa * TurretDrone::BARREL_LEN);
  }
  mb.end();
  glLineWidth(3.0f);
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();

  // Hub dots in one point batch.
  mb.clear();
  mb.begin(GL_POINTS);
  for(auto &t : ship->turrets) {
    mb.color(color[0], color[1], color[2]);
    mb.vertex(t.position.x(), t.position.y());
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw(4.0f);
}

void GLShip::draw_giga_mines(bool minimap) const {
  if(ship->giga_mines.empty()) return;

  static MeshBuilder mb;
  static Mesh mesh;

  if(minimap) {
    mb.clear();
    mb.begin(GL_POINTS);
    mb.color(1.0f, 0.2f, 0.0f);
    for(auto &m : ship->giga_mines) {
      mb.vertex(m.position.x(), m.position.y());
    }
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw(3.0f);
    return;
  }

  float size = 16.0f;
  float inner = size * 0.4f;

  // 8-pointed star spikes: all mines in one GL_LINES batch
  mb.clear();
  mb.begin(GL_LINES);
  mb.color(1.0f, 0.2f, 0.0f);
  for(auto &m : ship->giga_mines) {
    float angle = m.rotation * (float)M_PI / 180.0f;
    for(int i = 0; i < 8; i++) {
      float a  = angle + i * (float)M_PI / 4.0f;
      float a2 = angle + (i + 0.5f) * (float)M_PI / 4.0f;
      float a3 = angle + (i + 1.0f) * (float)M_PI / 4.0f;
      float ox  = cosf(a)  * size  + m.position.x(), oy  = sinf(a)  * size  + m.position.y();
      float ix  = cosf(a2) * inner + m.position.x(), iy  = sinf(a2) * inner + m.position.y();
      float ox2 = cosf(a3) * size  + m.position.x(), oy2 = sinf(a3) * size  + m.position.y();
      mb.vertex(ox, oy);   mb.vertex(ix, iy);
      mb.vertex(ix, iy);   mb.vertex(ox2, oy2);
    }
  }
  mb.end();
  glLineWidth(2.5f);
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();

  // Center circles: one GL_LINE_LOOP group per mine, all in one VBO
  mb.clear();
  for(auto &m : ship->giga_mines) {
    float angle = m.rotation * (float)M_PI / 180.0f;
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 0.2f, 0.0f);
    for(int i = 0; i < 8; i++) {
      float a = angle + i * (float)M_PI / 4.0f;
      mb.vertex(cosf(a) * inner * 0.6f + m.position.x(),
                sinf(a) * inner * 0.6f + m.position.y());
    }
    mb.end();
  }
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

void GLShip::draw_shockwaves() const {
  if(ship->shockwaves.empty()) return;

  static MeshBuilder mb_bright, mb_glow;
  static Mesh mesh_bright, mesh_glow;

  mb_bright.clear();
  mb_glow.clear();

  const int segs = 64;
  for(auto &sw : ship->shockwaves) {
    if(!sw.alive()) continue;
    float alpha = sw.time_left / 700.0f;
    if(alpha > 1.0f) alpha = 1.0f;
    if(alpha < 0.0f) alpha = 0.0f;

    // Bright expanding ring
    mb_bright.begin(GL_LINE_LOOP);
    mb_bright.color(1.0f, 0.6f, 0.1f, alpha);
    for(int i = 0; i < segs; i++) {
      float a = i * 2.0f * (float)M_PI / segs;
      mb_bright.vertex(sw.position.x() + cosf(a) * sw.radius,
                       sw.position.y() + sinf(a) * sw.radius);
    }
    mb_bright.end();

    // Slightly-larger translucent glow ring
    float r2 = sw.radius * 1.06f;
    mb_glow.begin(GL_LINE_LOOP);
    mb_glow.color(1.0f, 0.3f, 0.0f, alpha * 0.4f);
    for(int i = 0; i < segs; i++) {
      float a = i * 2.0f * (float)M_PI / segs;
      mb_glow.vertex(sw.position.x() + cosf(a) * r2,
                     sw.position.y() + sinf(a) * r2);
    }
    mb_glow.end();
  }

  glLineWidth(3.0f);
  mesh_bright.upload(mb_bright, GL_DYNAMIC_DRAW);
  mesh_bright.draw();

  glLineWidth(1.5f);
  mesh_glow.upload(mb_glow, GL_DYNAMIC_DRAW);
  mesh_glow.draw();
}

void GLShip::draw_shocks() const {
  if(ship->shocks.empty()) return;

  static MeshBuilder mb_core, mb_glow, mb_spark;
  static Mesh mesh_core, mesh_glow, mesh_spark;

  mb_core.clear();
  mb_glow.clear();
  mb_spark.clear();

  for(auto &b : ship->shocks) {
    if(b.points.size() < 2) continue;
    float alpha = b.growing ? 1.0f : b.life;
    if(alpha > 1.0f) alpha = 1.0f;
    if(alpha <= 0.0f) continue;

    // Wide translucent halo
    mb_glow.begin(GL_LINE_STRIP);
    mb_glow.color(0.35f, 0.65f, 1.0f, alpha * 0.35f);
    for(auto &p : b.points) mb_glow.vertex(p.x(), p.y());
    mb_glow.end();

    // Bright blue-white core
    mb_core.begin(GL_LINE_STRIP);
    mb_core.color(0.85f, 0.92f, 1.0f, alpha);
    for(auto &p : b.points) mb_core.vertex(p.x(), p.y());
    mb_core.end();
  }

  // Spark burst where an arc was absorbed by something it couldn't destroy:
  // short rays fanning out from the collision point, hot at the centre and
  // fading to transparent at their tips.
  bool any_spark = false;
  for(auto &b : ship->shocks) {
    if(b.spark_life <= 0.0f || b.spark_rays.empty()) continue;
    any_spark = true;
    float sa = b.spark_life; if(sa > 1.0f) sa = 1.0f;
    float grow = 0.5f + 0.5f * b.spark_life;  // rays retract a little as they fade
    mb_spark.begin(GL_LINES);
    for(auto &r : b.spark_rays) {
      mb_spark.color(0.9f, 0.95f, 1.0f, sa);
      mb_spark.vertex(b.spark_pos.x(), b.spark_pos.y());
      mb_spark.color(0.4f, 0.7f, 1.0f, 0.0f);
      mb_spark.vertex(b.spark_pos.x() + r.x() * grow, b.spark_pos.y() + r.y() * grow);
    }
    mb_spark.end();
  }

  // Additive so overlapping forks read as hot electric light.
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glLineWidth(5.0f);
  mesh_glow.upload(mb_glow, GL_DYNAMIC_DRAW);
  mesh_glow.draw();

  glLineWidth(2.0f);
  mesh_core.upload(mb_core, GL_DYNAMIC_DRAW);
  mesh_core.draw();

  if(any_spark) {
    glLineWidth(2.5f);
    mesh_spark.upload(mb_spark, GL_DYNAMIC_DRAW);
    mesh_spark.draw();
  }
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GLShip::draw_missiles() const {
  if(ship->missiles.empty()) return;

  static MeshBuilder mb;
  static Mesh mesh;

  // All missile trails batched into one GL_POINTS draw
  mb.clear();
  mb.begin(GL_POINTS);
  for(auto &m : ship->missiles) {
    int trail_sz = (int)m.trail.size();
    for(int ti = trail_sz - 1; ti >= 0; ti--) {
      float alpha = 1.0f - (float)ti / (float)trail_sz;
      mb.color(color[0], color[1], color[2], alpha);
      mb.vertex(m.trail[ti].x(), m.trail[ti].y());
    }
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw(4.0f);

  // Missile bodies: pre-built triangle mesh, one draw per missile via draw_at
  glLineWidth(1.5f);
  for(auto &m : ship->missiles) {
    missile_body.draw_at(m.position.x(), m.position.y(), m.facing.direction());
  }
}

