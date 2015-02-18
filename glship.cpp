#include "glship.h"
#include "gltrail.h"
#include "ship.h"
#include "typer.h"
#include "teleport.h"
#include "weapon/base.h"
#include <math.h>
#include <SDL.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <list>
#include <iostream>
#include <cstring>

using namespace std;

GLShip::GLShip(const Grid &grid, bool has_friction) : show_help(false) {
  //TODO: load config from file (colours too)
  ship = new Ship(grid, has_friction);
  trails.push_back(new GLTrail(this, 0.01, Point(0,0), 0.3,0.0, GLTrail::THRUSTING, 2500.0));
  trails.push_back(new GLTrail(this, 0.5,Point(-4,17),-0.1, 0.9, GLTrail::REVERSING | GLTrail::RIGHT, 250.0));
  trails.push_back(new GLTrail(this, 0.5,Point( 4,17),-0.1,-0.9, GLTrail::REVERSING | GLTrail::LEFT, 250.0));

  rotating_view = true;
  camera_rotation = ship->heading();

  camera_angle = 85.0f;

  color[0] = 72/255.0;
  color[1] = 118/255.0;
  color[2] = 255/255.0;

  body = glGenLists(1);
  glNewList(body, GL_COMPILE);
	glVertex2f( 0.0f, 1.0f);
	glVertex2f(-0.8f,-1.0f);
	glVertex2f( 0.0f,-0.5f);
	glVertex2f( 0.8f,-1.0f);
  glEndList();

  jets = glGenLists(1);
  glNewList(jets, GL_COMPILE);
  glColor3f( 1.0f-color[0], 1.0f-color[1], 1.0f-color[2] );
	glBegin(GL_QUADS);
	glVertex2f( 0.0f,-0.5f );
	glVertex2f(-0.4f,-0.75f );
	glVertex2f( 0.0f,-1.5f );
	glVertex2f( 0.4f,-0.75f );
	glEnd();
  glEndList();

  genForceShield();
  genRepulsor();
}

void GLShip::genRepulsor() {
  repulsors = glGenLists(1);
  glNewList(repulsors, GL_COMPILE);
  glColor3f( 1.0f-color[0], 1.0f-color[1], 1.0f-color[2] );
	glBegin(GL_QUADS);
	glVertex2f( 0.3f,  0.3f );
	glVertex2f( 0.6f,  0.9f );
	glVertex2f( 0.9f,  0.9f );
	glVertex2f( 0.75f, 0.3f );
	glEnd();
  glEndList();
}

void GLShip::genForceShield() {
  force_shield_bg = glGenLists(1);
  glNewList(force_shield_bg, GL_COMPILE);
  glColor4f(color[0], color[1], color[2], 0.5f);
  glBegin(GL_POLYGON);
  int number_of_segments = 20;
  float segment_size = 360.0/number_of_segments, d;
  float shield_size = 2;
  for (float i = 0.0; i < 360.0; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(cos(d)*shield_size, sin(d)*shield_size);
  }
  glEnd();
  glEndList();
  force_shield = glGenLists(1);
  glNewList(force_shield, GL_COMPILE);
  glPointSize(15.0f);
  glColor4f(color[0], color[1], color[2], 1.0f);
  glBegin(GL_LINE_LOOP);
  for (float i = 0.0; i < 360.0; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(cos(d)*shield_size, sin(d)*shield_size);
  }
  glEnd();
  glEndList();
}

GLShip::~GLShip() {
  //TODO: Make lists static for class (or ShipType or something)
  glDeleteLists(body, 1);
  glDeleteLists(jets, 1);
  glDeleteLists(repulsors, 1);
  glDeleteLists(force_shield, 1);
  delete ship;
  while(!trails.empty()) {
    delete trails.back();
    trails.pop_back();
  }
}

float GLShip::camera_facing() const {
  return -camera_rotation;
}

void GLShip::collide_grid(Grid &grid) {
    ship->collide_grid(grid);
    for(list<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
        (*i)->collide_grid(grid);
    }
}

void GLShip::collide(GLShip* first, GLShip* second) {
  Ship::collide(first->ship, second->ship);
}

void GLShip::step(float delta, const Grid &grid) {
  ship->step(delta, grid);

  float camera_rotation_delta = ship->heading() - camera_rotation;
  while(camera_rotation_delta < -90)
    camera_rotation_delta += 360;
  while(camera_rotation_delta > 270)
    camera_rotation_delta -= 360;
  camera_rotation += camera_rotation_delta * delta * 0.004;
  //std::cout << (ship->heading() - camera_rotation) << "\t" << camera_rotation << " " << ship->heading() << " " << delta << std::endl;

  for(list<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
    (*i)->step(delta);
  }
}

void GLShip::set_keys(int left, int right, int thrust, int shoot, int reverse, int mine, int next_weapon, int boost, int teleport, int help) {
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
}

void GLShip::set_controller(SDL_GameController *game_controller) {
  controller = game_controller;
}

void GLShip::draw_temperature() const {
  if(ship->heat_rate <= 0.0f)
    return;
  float height = 5.0, width = 1.0;

  /* temperature */
  float color[3] = {0,1.0,0};
  color[1] *= 1.0 - ship->temperature_ratio();
  color[0] = ship->temperature_ratio();
  glColor3fv(color);
  glScalef(width, height, 1.0f);
  glBegin(GL_POLYGON);
  glVertex2f(0.0f, 0.0f);
  glVertex2f(1.0f, 0.0f);
  float temp = temperature() > critical_temperature() ? critical_temperature() : temperature();
  glVertex2f(1.0f, temp/max_temperature());
  glVertex2f(0.0f, temp/max_temperature());
  glEnd();

  if(temperature() > critical_temperature()) {
    glPushMatrix();
    glColor3f(1.0f,0.0f,0.0f);
    glTranslatef(0.0f, critical_temperature()/max_temperature(), 0.0f);
    glScalef(1.0f, 0.5f, 1.0f);
    glBegin(GL_POLYGON);
    glVertex2f( 0.0f, 0.0f);
    glVertex2f( 1.0f, 0.0f);
    temp = temperature()/max_temperature()-critical_temperature()/max_temperature();
    glVertex2f( 1.0f, temp);
    glVertex2f( 0.0f, temp);
    glEnd();
    glPopMatrix();
  }

  /* border */
  glColor3f(1,1,1);
  glBegin(GL_LINE_LOOP);
  glVertex2i(0.0f,0.0f);
  glVertex2i(1.0f,0.0f);
  glVertex2i(1.0f,1.0f);
  glVertex2i(0.0f,1.0f);
  glEnd();

  glBegin(GL_LINES);
  glVertex2f(0.0f, critical_temperature()/max_temperature());
  glVertex2f(1.0f, critical_temperature()/max_temperature());
  glEnd();
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
  bool pressed = event.cbutton.state == SDL_PRESSED;
  if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
    show_help = pressed;
  }
  if(!ship->is_alive()) {
    if(pressed && event.cbutton.button == SDL_CONTROLLER_BUTTON_A && ship->lives > 0) {
      ship->time_until_respawn = 0;
    } else {
      return;
    }
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
    ship->mine(pressed);
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER && pressed) {
    ship->boost();
  } else if(event.cbutton.button == SDL_CONTROLLER_BUTTON_X && pressed) {
    ship->next_weapon();
  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && pressed) {
    ship->behaviours.push_back(new Teleport(ship));
  }
}

void GLShip::controller_axis_input(SDL_Event event) {
  if(!wasMyController(event.cbutton.which)) {
    return;
  }
  if(!ship->is_alive()) {
    return;
  }
  Sint16 deadzone = 10000;
  bool pressed = event.caxis.value > 10000; // deadzone // SDL_CONTROLLER_AXIS_MAX

  if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
    if(event.caxis.value > deadzone) {
      ship->rotate_right(true);
    } else if (event.caxis.value < -deadzone) {
      ship->rotate_left(true);
    } else {
      ship->rotate_left(false);
      ship->rotate_right(false);
    }
  } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
    if(event.caxis.value > deadzone) {
      ship->reverse(true);
    } else if (event.caxis.value < -deadzone) {
      ship->thrust(true);
    } else {
      ship->thrust(false);
      ship->reverse(false);
    }
  }
}

void GLShip::input(unsigned char key, bool pressed) {
  if (key == help_key && pressed) show_help = !show_help;
  if(!ship->is_alive()) {
    if(key == shoot_key && ship->lives > 0) {
      ship->time_until_respawn = 0;
    } else {
      return;
    }
  }
  if (key == left_key) {
    ship->rotate_left(pressed);
  } else if (key == right_key) {
    ship->rotate_right(pressed);
  } else if (key == thrust_key) {
    ship->thrust(pressed);
  } else if (key == reverse_key) {
    ship->reverse(pressed);
  } else if (key == shoot_key) {
    ship->shoot(pressed);
  } else if (key == mine_key) {
    ship->mine(pressed);
  } else if (key == boost_key && pressed) {
    ship->boost();
  } else if(key == next_weapon_key && pressed) {
    ship->next_weapon();
  } else if (key == 'q' && pressed) {
    ship->disable_behaviours();
  } else if (key == teleport_key && pressed) {
    ship->behaviours.push_back(new Teleport(ship));
  } else if (key == 'v' && pressed) {
    rotating_view = !rotating_view;
  } else if (key == 'z' && pressed) {
    ship->time_left_invincible += 1000;
    ship->invincible = true;
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
  if(ship->is_alive()) {
    draw_ship(minimap);
  }
}

void GLShip::draw_ship(bool minimap) const {
  glTranslatef(ship->position.x(), ship->position.y(), 0.0f);
  glScalef( ship->radius, ship->radius, 1.0f);
  glRotatef( ship->heading(), 0.0f, 0.0f, 1.0f);

  if(minimap) {
    glColor3fv(color);
    glPointSize(5.0f);
    glBegin(GL_POINTS);
    glVertex2f(0.0f, 0.0f);
    glEnd();
    return;
  }

  glPointSize(2.5f);
  glLineWidth(1.8f);

  if(ship->thrusting) {
    glCallList(jets);
  }

  if(ship->reversing) {
    glPushMatrix();
    glCallList(repulsors);
    glRotatef(180, 0, 1, 0);
    glCallList(repulsors);
    glPopMatrix();
  }

  draw_body();

  if(ship->invincible) {
    glCallList(force_shield);
  }

  if(ship->invincible) {
    glCallList(force_shield_bg);
  }
}

void GLShip::draw_body() const {
  glBegin(GL_POLYGON);
  glColor3f(0.0f,0.0f,0.0f);
  glCallList(body);
  glEnd();

  glColor3fv(color);
  glBegin(GL_LINE_LOOP);
  glCallList(body);
  glEnd();
}

void GLShip::draw_keymap() const {
  int size = 10;
  int num_controls  = 8;
  if(controller != NULL) {
    num_controls++;
  }
  int padding = 2.0f;
  int char_height = 5.0f;
  float y_offset = 170.0f; // above minimap
  Typer::draw_centered(0, (num_controls+1.5)/2.0f * (size + padding) * char_height + y_offset, "- PLAYER -", size+2);
  float offset = -160.0f;
  int control_index = 0;
  if(controller != NULL) {
    Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "MOVE", size);
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "LEFT STICK", size);
    control_index++;
  }
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "THRUST", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)thrust_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_DPAD_UP), size);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "REVERSE", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)reverse_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN), size);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TURN RIGHT", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)right_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT), size);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TURN LEFT", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)left_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT), size);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "SHOOT", size);
  if(controller == NULL && shoot_key == ' ') {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "SPACE", size);
  } else if (controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)shoot_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_A), size);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "MINE", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)mine_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_B), size);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "CHANGE WEAPON", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)next_weapon_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_X), size);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "BOOST", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)boost_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER), size);
  }
  control_index++;
  Typer::draw(offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, "TELEPORT", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, (char)teleport_key, size);
  } else {
    Typer::draw(-offset, (num_controls-control_index)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), size);
  }
  control_index++;

  int common_offset = control_index+1;
  Typer::draw_centered(0, (num_controls-common_offset )/2.0f * (size + padding) * char_height + y_offset, "- GAME -", size +2);
  Typer::draw(offset, (num_controls-common_offset-1.5)/2.0f * (size + padding) * char_height + y_offset, "PAUSE", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-common_offset-1.5)/2.0f * (size + padding) * char_height + y_offset, 'p', size);
  } else {
    Typer::draw(-offset, (num_controls-common_offset-1.5)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_START), size);
  }
  Typer::draw(offset, (num_controls-common_offset-2.5)/2.0f * (size + padding) * char_height + y_offset, "FULLSCREEN", size);
  Typer::draw(-offset, (num_controls-common_offset-2.5)/2.0f * (size + padding) * char_height + y_offset, 'f', size);
  Typer::draw(offset, (num_controls-common_offset-3.5)/2.0f * (size + padding) * char_height + y_offset, "FRIENDLY FIRE", size);
  Typer::draw(-offset, (num_controls-common_offset-3.5)/2.0f * (size + padding) * char_height + y_offset, 'g', size);
  Typer::draw(offset, (num_controls-common_offset-4.5)/2.0f * (size + padding) * char_height + y_offset, "HIDE THIS", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-common_offset-4.5)/2.0f * (size + padding) * char_height + y_offset, "f1", size);
  } else if(help_key == 128 + GLUT_KEY_F8) {
    Typer::draw(-offset, (num_controls-common_offset-4.5)/2.0f * (size + padding) * char_height + y_offset, "f8", size);
  } else {
    Typer::draw(-offset, (num_controls-common_offset-4.5)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_GUIDE), size);
  }
  Typer::draw(offset, (num_controls-common_offset-5.5)/2.0f * (size + padding) * char_height + y_offset, "QUIT", size);
  if(controller == NULL) {
    Typer::draw(-offset, (num_controls-common_offset-5.5)/2.0f * (size + padding) * char_height + y_offset, "ESC", size);
  } else {
    Typer::draw(-offset, (num_controls-common_offset-5.5)/2.0f * (size + padding) * char_height + y_offset, SDL_GameControllerGetStringForButton(SDL_CONTROLLER_BUTTON_BACK), size);
  }

  int cheat_offset = common_offset + 7;
  Typer::draw_centered(0, (num_controls-cheat_offset-0.5)/2.0f * (size + padding) * char_height + y_offset, "- CHEATS -", size +2);
  Typer::draw(offset, (num_controls-cheat_offset-2)/2.0f * (size + padding) * char_height + y_offset, "SPEED UP", size);
  Typer::draw(-offset, (num_controls-cheat_offset-2)/2.0f * (size + padding) * char_height + y_offset, '+', size);
  Typer::draw(offset, (num_controls-cheat_offset-3)/2.0f * (size + padding) * char_height + y_offset, "SLOW DOWN", size);
  Typer::draw(-offset, (num_controls-cheat_offset-3)/2.0f * (size + padding) * char_height + y_offset, '-', size);
  Typer::draw(offset, (num_controls-cheat_offset-4)/2.0f * (size + padding) * char_height + y_offset, "RESET SPEED", size);
  Typer::draw(-offset, (num_controls-cheat_offset-4)/2.0f * (size + padding) * char_height + y_offset, '0', size);
  Typer::draw(offset, (num_controls-cheat_offset-5)/2.0f * (size + padding) * char_height + y_offset, "SKIP LEVEL", size);
  Typer::draw(-offset, (num_controls-cheat_offset-5)/2.0f * (size + padding) * char_height + y_offset, 'n', size);
}

void GLShip::draw_weapons() const {
  int x = 20;
  int y = -20;
  Typer::draw(x, y, "Weapons", 15);
  Weapon::Base *weapon = *(ship->primary);
  if(weapon != NULL && !ship->primary_weapons.empty()) {
    Typer::draw(x+10,y-55,weapon->name(),10);
    if(!weapon->is_unlimited()) {
      if(weapon->ammo() == 0) {
        Typer::draw(x+30+20*strlen(weapon->name()),y-55,"empty",10);
      } else {
        Typer::draw_lefted(x+50+20*strlen(weapon->name()),y-55,weapon->ammo(),10);
      }
    }
  }
  weapon = *(ship->secondary);
  if(weapon != NULL && !ship->secondary_weapons.empty()) {
    Typer::draw(x+10,y-95,weapon->name(),10);
    if(!weapon->is_unlimited()) {
      if(weapon->ammo() == 0) {
        Typer::draw(x+30+20*strlen(weapon->name()),y-95,"empty",10);
      } else {
        Typer::draw_lefted(x+50+20*strlen(weapon->name()),y-95,weapon->ammo(),10);
      }
    }
  }
}

void GLShip::draw_particles() const {
  glColor3fv(color);
  glPointSize(3.5f);
  glLineWidth(2.5f);
  //TODO: ParticleDrawer::draw(ship->bullets);
  glBegin(GL_LINES);
  for(list<Particle>::iterator b = ship->bullets.begin(); b != ship->bullets.end(); b++) {
    //TODO: Work out how to make bullets draw themselves. GLBullet?
    glVertex2fv(b->position - b->velocity*10);
    glVertex2fv(b->position);
  }
  glEnd();
}

bool GLShip::is_removable() const {
  return ship->is_removable();
}

void GLShip::draw_debris() const {
  glPointSize(2.5f);
  glBegin(GL_POINTS);
  for(list<Particle>::iterator d = ship->debris.begin(); d != ship->debris.end(); d++) {
    glColor4f(color[0], rand()/(2.0f*(float)RAND_MAX)+0.5, color[2], d->aliveness());
		glVertex2fv(d->position);
  }
	glEnd();
}

void GLShip::draw_mines(bool minimap) const {
  if(minimap) {
    glPointSize(1.5f);
    glColor3fv(color);
    glBegin(GL_POINTS);
    for(list<Particle>::iterator m = ship->mines.begin(); m != ship->mines.end(); m++) {
      glVertex2fv(m->position);
    }
    glEnd();
    return;
  }

  float size = 7.5;
  glPointSize(5.0f);
  glLineWidth(2.0f);
  for(list<Particle>::iterator m = ship->mines.begin(); m != ship->mines.end(); m++) {
    glPushMatrix();
    glTranslatef(m->position.x(), m->position.y(), 0.0f);
    glRotated((glutGet(GLUT_ELAPSED_TIME) - m->time_left)/-8.0, 0.0f, 0.0f, 1.0f);
    glBegin(GL_LINE_LOOP);
    glColor3fv(color);
  	glVertex2fv(Point(0,-size));
  	glVertex2fv(Point(size,0));
    glVertex2fv(Point(0,size));
  	glVertex2fv(Point(-size,0));
  	glEnd();
    glPopMatrix();
  }
}

