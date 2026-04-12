#include "glship.h"
#include "gltrail.h"
#include "ship.h"
#include "typer.h"
#include "teleport.h"
#include "weapon/base.h"
#include "weapon/god_mode.h"
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

GLShip::GLShip(const Grid &grid, bool has_friction) : show_help(false), last_input_was_controller(false) {
  //TODO: load config from file (colours too)
  ship = new Ship(grid, has_friction);
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
  float camera_rotation_delta = ship->heading() - camera_rotation;
  while(camera_rotation_delta < -90)
    camera_rotation_delta += 360;
  while(camera_rotation_delta > 270)
    camera_rotation_delta -= 360;
  camera_rotation += camera_rotation_delta * frame_delta * 0.004;
}

void GLShip::step(int delta, const Grid &grid) {
  ship->step(delta, grid);

  for(list<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
    (*i)->step(delta);
  }
}

void GLShip::set_keys(int left, int right, int thrust, int shoot, int reverse, int mine, int next_weapon, int boost, int teleport, int help, int next_secondary, int toggle_rotate_view) {
  left_key = left;
  right_key = right;
  shoot_key = shoot;
  thrust_key = thrust;
  teleport_key = teleport;
  reverse_key = reverse;
  mine_key = mine;
  next_weapon_key = next_weapon;
  boost_key = boost;
  help_key = help;
  next_secondary_key = next_secondary;
  toggle_rotate_view_key = toggle_rotate_view;
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
  if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
    show_help = pressed;
  }
  if(!ship->is_alive()) {
    if(pressed && event.cbutton.button == SDL_CONTROLLER_BUTTON_A && ship->lives > 0 &&
       ship->time_until_respawn <= ship->respawn_time - 1000) {
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
    ship->shoot(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
    ship->fire_secondary(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER && pressed) {
    ship->boost();
  } else if(event.cbutton.button == SDL_CONTROLLER_BUTTON_X && pressed) {
    ship->next_weapon();
  } else if(event.cbutton.button == SDL_CONTROLLER_BUTTON_Y && pressed) {
    ship->next_secondary_weapon();
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && pressed) {
    ship->behaviours.push_back(new Teleport(ship));
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
      ship->shoot(pressed);
    }
  } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
    bool pressed = event.caxis.value > 8000;
    if(pressed != l2_shoot_active) {
      if(pressed) last_input_was_controller = true;
      l2_shoot_active = pressed;
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

void GLShip::input(unsigned char key, bool pressed) {
  if (key == help_key && pressed) show_help = !show_help;
  if(!ship->is_alive()) {
    kb_thrust = kb_reverse = kb_rotate_left = kb_rotate_right = false;
    if(key == shoot_key && ship->lives > 0 &&
       ship->time_until_respawn <= ship->respawn_time - 1000) {
      last_input_was_controller = false;
      ship->time_until_respawn = 0;
    }
    return;
  }
  if (key == left_key || key == right_key || key == thrust_key || key == reverse_key ||
      key == shoot_key || key == mine_key || key == boost_key || key == next_weapon_key ||
      key == next_secondary_key || key == teleport_key || key == help_key ||
      key == (unsigned char)toggle_rotate_view_key) {
    last_input_was_controller = false;
  }
  if (key == left_key) {
    kb_rotate_left = pressed;
    ship->rotate_left(pressed);
  } else if (key == right_key) {
    kb_rotate_right = pressed;
    ship->rotate_right(pressed);
  } else if (key == thrust_key) {
    kb_thrust = pressed;
    ship->thrust(pressed);
  } else if (key == reverse_key) {
    kb_reverse = pressed;
    ship->reverse(pressed);
  } else if (key == shoot_key) {
    ship->shoot(pressed);
  } else if (key == mine_key) {
    ship->fire_secondary(pressed);
  } else if (key == boost_key && pressed) {
    ship->boost();
  } else if(key == next_weapon_key && pressed) {
    ship->next_weapon();
  } else if(key == next_secondary_key && pressed) {
    ship->next_secondary_weapon();
  } else if (key == teleport_key && pressed) {
    ship->behaviours.push_back(new Teleport(ship));
  } else if (key == (unsigned char)toggle_rotate_view_key && pressed) {
    rotating_view = !rotating_view;
    g_prefs.rotate_view = rotating_view;
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
  if(!minimap) {
    draw_missiles();
    draw_shockwaves();
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
  if (key == ' ')  return "SPACE";
  if (key == 27)   return "ESC";
  if (key == 13)   return "ENTER";
  if (key == 9)    return "TAB";
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

void GLShip::draw_keymap() const {
  int size = 10;
  int num_controls  = 10;
  if(last_input_was_controller) {
    num_controls++;
  }
  int padding = 2.0f;
  int char_height = 5.0f;
  float y_offset = last_input_was_controller ? 110.0f : 140.0f; // above minimap
  Typer::draw_centered(0, (num_controls+1.5)/2.0f * (size + padding) * char_height + y_offset, "- PLAYER -", size+2);
  float offset = -160.0f;
  int control_index = 0;

  // Draw a controller button: circled glyph for single-char buttons (A/B/X/Y/…),
  // plain text for multi-char ones (dpup, leftshoulder, …).
  auto draw_btn = [&](float x, float y, SDL_GameControllerButton btn) {
    const char *s = SDL_GameControllerGetStringForButton(btn);
    if (strlen(s) == 1)
      Typer::draw_button(x, y, s[0], size);
    else
      Typer::draw(x, y, s, size);
  };

  if(last_input_was_controller) {
    Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "MOVE", size);
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "LEFT STICK", size);
    control_index++;
  }
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "THRUST", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(thrust_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_DPAD_UP);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "REVERSE", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(reverse_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TURN RIGHT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(right_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TURN LEFT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(left_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "SHOOT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(shoot_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_A);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "MINE", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(mine_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_B);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "CHANGE WEAPON", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(next_weapon_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_X);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "CHANGE SECONDARY", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(next_secondary_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_Y);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "BOOST", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(boost_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TELEPORT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(teleport_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  }
  control_index++;
  if(!last_input_was_controller) {
    Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "ROTATE VIEW", size);
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, key_label(toggle_rotate_view_key).c_str(), size);
  }
  control_index++;

  int common_offset = control_index+1;
  const GeneralKeys &gk = g_prefs.general_keys;
  Typer::draw_centered(0, (num_controls-common_offset )/2.0f * (size + padding) * char_height + y_offset, "- GAME -", size +2);
  Typer::draw(offset, (num_controls-common_offset-1.5)/2.0f * (size + padding) * char_height + y_offset, "PAUSE", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-common_offset-1.5)/2.0f * (size + padding) * char_height + y_offset, key_label(gk.pause).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-common_offset-1.5)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_START);
  }
  Typer::draw(offset, (num_controls-common_offset-2.5)/2.0f * (size + padding) * char_height + y_offset, "FULLSCREEN", size);
  Typer::draw(-offset, (num_controls-common_offset-2.5)/2.0f * (size + padding) * char_height + y_offset, key_label(gk.toggle_fullscreen).c_str(), size);
  Typer::draw(offset, (num_controls-common_offset-3.5)/2.0f * (size + padding) * char_height + y_offset, "FRIENDLY FIRE", size);
  Typer::draw(-offset, (num_controls-common_offset-3.5)/2.0f * (size + padding) * char_height + y_offset, key_label(gk.toggle_friendly_fire).c_str(), size);
  Typer::draw(offset, (num_controls-common_offset-4.5)/2.0f * (size + padding) * char_height + y_offset, "HIDE THIS", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-common_offset-4.5)/2.0f * (size + padding) * char_height + y_offset, key_label(help_key).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-common_offset-4.5)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_GUIDE);
  }
  Typer::draw(offset, (num_controls-common_offset-5.5)/2.0f * (size + padding) * char_height + y_offset, "QUIT", size);
  if(!last_input_was_controller) {
    Typer::draw(-offset, (num_controls-common_offset-5.5)/2.0f * (size + padding) * char_height + y_offset, key_label(gk.menu).c_str(), size);
  } else {
    draw_btn(-offset, (num_controls-common_offset-5.5)/2.0f * (size + padding) * char_height + y_offset, SDL_CONTROLLER_BUTTON_BACK);
  }

  int cheat_offset = common_offset + 7;
  Typer::draw_centered(0, (num_controls-cheat_offset-0.5)/2.0f * (size + padding) * char_height + y_offset, "- CHEATS -", size +2);
  Typer::draw(offset, (num_controls-cheat_offset-2)/2.0f * (size + padding) * char_height + y_offset, "SPEED UP", size);
  Typer::draw(-offset, (num_controls-cheat_offset-2)/2.0f * (size + padding) * char_height + y_offset, key_label(gk.time_speed_up).c_str(), size);
  Typer::draw(offset, (num_controls-cheat_offset-3)/2.0f * (size + padding) * char_height + y_offset, "SLOW DOWN", size);
  Typer::draw(-offset, (num_controls-cheat_offset-3)/2.0f * (size + padding) * char_height + y_offset, key_label(gk.time_slow_down).c_str(), size);
  Typer::draw(offset, (num_controls-cheat_offset-4)/2.0f * (size + padding) * char_height + y_offset, "RESET SPEED", size);
  Typer::draw(-offset, (num_controls-cheat_offset-4)/2.0f * (size + padding) * char_height + y_offset, key_label(gk.time_reset).c_str(), size);
  Typer::draw(offset, (num_controls-cheat_offset-5)/2.0f * (size + padding) * char_height + y_offset, "SKIP LEVEL", size);
  Typer::draw(-offset, (num_controls-cheat_offset-5)/2.0f * (size + padding) * char_height + y_offset, key_label(gk.skip_level).c_str(), size);
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
                             int cycle_key_kb, SDL_GameControllerButton cycle_btn,
                             int fire_key_kb,  SDL_GameControllerButton fire_btn) {
    // Line 1: NAME  ammo
    int cx = 10;
    Typer::draw(cx, row_y, weapon->name(), size);
    cx += (int)strlen(weapon->name()) * cw + 2 * cw;  // name + gap

    if (!weapon->is_unlimited()) {
      if (weapon->ammo() == 0) {
        Typer::draw(cx, row_y, "empty", size);
      } else {
        int display_ammo = dynamic_cast<Weapon::GodMode*>(weapon) ? weapon->ammo()/1000 : weapon->ammo();
        Typer::draw_lefted(cx + 2*cw, row_y, display_ammo, size);
      }
    }

    // Line 2: FIRE [key]   NEXT [key]
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
  };

  int y = -20;
  Typer::draw(10, y, "Weapons", 15);
  y -= 55;

  if (!ship->primary_weapons.empty()) {
    Weapon::Base *weapon = *(ship->primary);
    if (weapon != NULL) {
      draw_weapon_row(y, weapon, ship->primary_weapons.size() > 1,
        next_weapon_key,   SDL_CONTROLLER_BUTTON_X,
        shoot_key,         SDL_CONTROLLER_BUTTON_A);
    }
  }

  if (!ship->secondary_weapons.empty()) {
    Weapon::Base *weapon = *(ship->secondary);
    if (weapon != NULL) {
      draw_weapon_row(y - 80, weapon, ship->secondary_weapons.size() > 1,
        next_secondary_key, SDL_CONTROLLER_BUTTON_Y,
        mine_key,           SDL_CONTROLLER_BUTTON_B);
    }
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
      } else {
        mb.color(color[0], color[1], color[2]);
      }
      Point tail = b->position - b->velocity * 10;
      mb.vertex(tail.x(), tail.y());
      mb.vertex(b->position.x(), b->position.y());
    }
    mb.end();
    glLineWidth(2.5f);
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
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

  mb.clear();
  mb.begin(GL_POINTS);
  for(auto d = ship->debris.begin(); d != ship->debris.end(); d++) {
    mb.color(color[0], flicker[idx++ % 64], color[2], d->aliveness());
    mb.vertex(d->position.x(), d->position.y());
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw(2.5f);
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

