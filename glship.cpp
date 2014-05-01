#include "glship.h"
#include "gltrail.h"
#include "ship.h"
#include "typer.h"
#include "teleport.h"
#include "weapon/base.h"
#include <math.h>

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

GLShip::GLShip(const Grid &grid, bool has_friction) {
  //TODO: load config from file (colours too)
  ship = new Ship(grid, has_friction);
  trails.push_back(new GLTrail(ship, 0.01, Point(0,0), 0.3,0.0, GLTrail::THRUSTING, 5000.0));
  trails.push_back(new GLTrail(ship, 0.5,Point(-4,17),-0.1, 0.9, GLTrail::REVERSING | GLTrail::RIGHT, 500.0));
  trails.push_back(new GLTrail(ship, 0.5,Point( 4,17),-0.1,-0.9, GLTrail::REVERSING | GLTrail::LEFT, 500.0));

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
  glColor3f( 1.0f, 1.0f, 1.0f );
	glBegin(GL_QUADS);
	glVertex2f( 0.0f,-0.5f );
	glVertex2f(-0.4f,-0.75f );
	glVertex2f( 0.0f,-1.5f );
	glVertex2f( 0.4f,-0.75f );
	glEnd();
  glEndList();

  force_shield = glGenLists(1);
  glNewList(force_shield, GL_COMPILE);
  glColor4f(0.0f, 0.0f, 0.0f, 0.3f);
  glBegin(GL_POLYGON);
  int number_of_segments = 20;
  float segment_size = 360.0/number_of_segments, d;
  float shield_size = 2;
  for (float i = 0.0; i < 360.0; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(cos(d)*shield_size, sin(d)*shield_size);
  }
  glEnd();
  glBegin(GL_LINE_LOOP);
  glColor4f(color[0], color[1], color[2], 0.5f);
  for (float i = 0.0; i < 360.0; i+= segment_size) {
    d = i*M_PI/180;
    glVertex2f(cos(d)*shield_size, sin(d)*shield_size);
  }
  glEnd();
  glEndList();

  repulsors = glGenLists(1);
  glNewList(repulsors, GL_COMPILE);
  glColor3f( 1.0f, 1.0f, 1.0f );
	glBegin(GL_QUADS);
	glVertex2f( 0.3f,  0.3f );
	glVertex2f( 0.6f,  0.9f );
	glVertex2f( 0.9f,  0.9f );
	glVertex2f( 0.75f, 0.3f );
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

void GLShip::set_keys(int left, int right, int thrust, int shoot, int reverse, int mine, int next_weapon, int boost) {
  left_key = left;
  right_key = right;
  shoot_key = shoot;
  thrust_key = thrust;
  reverse_key = reverse;
  mine_key = mine;
  next_weapon_key = next_weapon;
  boost_key = boost;
}

//void GLShip::draw_controls() const {
  //TODO: implement overlay of key controls
  // or name too intuuitive to need it
//}

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

void GLShip::input(unsigned char key, bool pressed) {
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
  } else if (key == 'z' && pressed) {
    ship->disable_behaviours();
  } else if (key == 't' && pressed) {
    ship->behaviours.push_back(new Teleport(ship));
  } else if (key == 'v' && pressed) {
    rotating_view = !rotating_view;
  } else if (key == 'e' && pressed) {
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

  if(ship->invincible) {
    glCallList(force_shield);
  }

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
  int padding = 2.0f;
  int char_height = 5.0f;
  Typer::draw_centered(0, (num_controls+1.5)/2.0f * (size + padding) * char_height, "- PLAYER -", size+2);
  glTranslatef(-160.0f,0.0f,0.0f);
  Typer::draw(0, num_controls/2.0f * (size + padding) * char_height, "THRUST", size);
  Typer::draw(320, num_controls/2.0f * (size + padding) * char_height, (char)thrust_key, size);
  Typer::draw(0, (num_controls-1)/2.0f * (size + padding) * char_height, "REVERSE", size);
  Typer::draw(320, (num_controls-1)/2.0f * (size + padding) * char_height, (char)reverse_key, size);
  Typer::draw(0, (num_controls-2)/2.0f * (size + padding) * char_height, "TURN RIGHT", size);
  Typer::draw(320, (num_controls-2)/2.0f * (size + padding) * char_height, (char)right_key, size);
  Typer::draw(0, (num_controls-3)/2.0f * (size + padding) * char_height, "TURN LEFT", size);
  Typer::draw(320, (num_controls-3)/2.0f * (size + padding) * char_height, (char)left_key, size);
  Typer::draw(0, (num_controls-4)/2.0f * (size + padding) * char_height, "SHOOT", size);
  if(shoot_key == ' ') {
    Typer::draw(320, (num_controls-4)/2.0f * (size + padding) * char_height, "SPACE", size);
  } else {
    Typer::draw(320, (num_controls-4)/2.0f * (size + padding) * char_height, (char)shoot_key, size);
  }
  Typer::draw(0, (num_controls-5)/2.0f * (size + padding) * char_height, "MINE", size);
  Typer::draw(320, (num_controls-5)/2.0f * (size + padding) * char_height, (char)mine_key, size);
  Typer::draw(0, (num_controls-6)/2.0f * (size + padding) * char_height, "CHANGE WEAPON", size);
  Typer::draw(320, (num_controls-6)/2.0f * (size + padding) * char_height, (char)next_weapon_key, size);
  Typer::draw(0, (num_controls-7)/2.0f * (size + padding) * char_height, "BOOST", size);
  Typer::draw(320, (num_controls-7)/2.0f * (size + padding) * char_height, (char)boost_key, size);


  Typer::draw_centered(160.0f, (num_controls-9)/2.0f * (size + padding) * char_height, "- COMMON -", size +2);
  Typer::draw(0, (num_controls-10.5)/2.0f * (size + padding) * char_height, "PAUSE", size);
  Typer::draw(320, (num_controls-10.5)/2.0f * (size + padding) * char_height, 'p', size);
  Typer::draw(0, (num_controls-11.5)/2.0f * (size + padding) * char_height, "FULLSCREEN", size);
  Typer::draw(320, (num_controls-11.5)/2.0f * (size + padding) * char_height, 'f', size);
  Typer::draw(0, (num_controls-12.5)/2.0f * (size + padding) * char_height, "FRIENDLY FIRE", size);
  Typer::draw(320, (num_controls-12.5)/2.0f * (size + padding) * char_height, 'g', size);
  Typer::draw(0, (num_controls-13.5)/2.0f * (size + padding) * char_height, "HIDE THIS", size);
  Typer::draw(320, (num_controls-13.5)/2.0f * (size + padding) * char_height, "f1", size);
  Typer::draw(0, (num_controls-14.5)/2.0f * (size + padding) * char_height, "QUIT", size);
  Typer::draw(320, (num_controls-14.5)/2.0f * (size + padding) * char_height, "ESC", size);

  Typer::draw_centered(160.0f, (num_controls-16.5)/2.0f * (size + padding) * char_height, "- CHEATS -", size +2);
  Typer::draw(0, (num_controls-18)/2.0f * (size + padding) * char_height, "SPEED UP", size);
  Typer::draw(320, (num_controls-18)/2.0f * (size + padding) * char_height, '+', size);
  Typer::draw(0, (num_controls-19)/2.0f * (size + padding) * char_height, "SLOW DOWN", size);
  Typer::draw(320, (num_controls-19)/2.0f * (size + padding) * char_height, '-', size);
  Typer::draw(0, (num_controls-20)/2.0f * (size + padding) * char_height, "RESET SPEED", size);
  Typer::draw(320, (num_controls-20)/2.0f * (size + padding) * char_height, '0', size);
  Typer::draw(0, (num_controls-21)/2.0f * (size + padding) * char_height, "SKIP LEVEL", size);
  Typer::draw(320, (num_controls-21)/2.0f * (size + padding) * char_height, 'n', size);
}

void GLShip::draw_weapons() const {
  Typer::draw(0,0,"Weapons",15);
  Typer::draw(0.0f,-50.0f,char(shoot_key),10);
  Weapon::Base *weapon = *(ship->primary);
  if(weapon != NULL && !ship->primary_weapons.empty()) {
    Typer::draw(20,-50,weapon->name(),10);
    if(!weapon->is_unlimited()) {
      if(weapon->ammo() == 0) {
        Typer::draw(40+20*strlen(weapon->name()),-50,"empty",10);
      } else {
        Typer::draw_lefted(60+20*strlen(weapon->name()),-50,weapon->ammo(),10);
      }
    }
  }
  weapon = *(ship->secondary);
  if(weapon != NULL && !ship->secondary_weapons.empty()) {
    Typer::draw(20,-90,weapon->name(),10);
    if(!weapon->is_unlimited()) {
      if(weapon->ammo() == 0) {
        Typer::draw(40+20*strlen(weapon->name()),-90,"empty",10);
      } else {
        Typer::draw_lefted(60+20*strlen(weapon->name()),-90,weapon->ammo(),10);
      }
    }
  }
}

void GLShip::draw_particles() const {
  glColor3fv(color);
  glPointSize(3.5f);
  //TODO: ParticleDrawer::draw(ship->bullets);
  glBegin(GL_POINTS);
  for(list<Particle>::iterator b = ship->bullets.begin(); b != ship->bullets.end(); b++) {
    //TODO: Work out how to make bullets draw themselves. GLBullet?
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

  float size = 20.0;
  glPointSize(5.0f);
  glLineWidth(3.0f);
  for(list<Particle>::iterator m = ship->mines.begin(); m != ship->mines.end(); m++) {
    glBegin(GL_LINE_STRIP);
    glColor4f(0,0,0,0);
  	glVertex2fv(m->position + Point(0,-size));
    glColor3fv(color);
  	glVertex2fv(m->position);
    glColor4f(0,0,0,0);
  	glVertex2fv(m->position + Point(0,size));
  	glEnd();
    glBegin(GL_LINE_STRIP);
    glColor4f(0,0,0,0);
  	glVertex2fv(m->position + Point(-size,0));
    glColor3fv(color);
  	glVertex2fv(m->position);
    glColor4f(0,0,0,0);
  	glVertex2fv(m->position + Point(size,0));
  	glEnd();
  }
}

