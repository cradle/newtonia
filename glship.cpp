#include "glship.h"
#include "gltrail.h"
#include "ship.h"
#include "typer.h"
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

using namespace std;

GLShip::GLShip() {
  //TODO: load config from file (colours too)
  ship = new Ship();
  trails.push_back(new GLTrail(ship, 0.01, Point(0,0), 0.25,0.0, GLTrail::THRUSTING, 5000.0));
  trails.push_back(new GLTrail(ship, 0.5,Point(-4,17),-0.1, 0.9, GLTrail::REVERSING | GLTrail::RIGHT, 500.0));
  trails.push_back(new GLTrail(ship, 0.5,Point( 4,17),-0.1,-0.9, GLTrail::REVERSING | GLTrail::LEFT, 500.0));
  
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
  glBegin(GL_POLYGON);
  int number_of_segments = 10;
  float segment_size = 360.0/number_of_segments, d;
  float shield_size = 2.5;
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

void GLShip::collide(GLShip* first, GLShip* second) {
  Ship::collide(first->ship, second->ship);
}

void GLShip::step(float delta) {
  ship->step(delta);

  for(list<GLTrail*>::iterator i = trails.begin(); i != trails.end(); i++) {
    (*i)->step(delta);
  }
}

void GLShip::set_keys(int left, int right, int thrust, int shoot, int reverse, int mine) {
  left_key = left;
  right_key = right;
  shoot_key = shoot;
  thrust_key = thrust;
  reverse_key = reverse;
  mine_key = mine;
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
      Typer::draw_centered(0,-3,ship->score,2);
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
  if(!ship->is_alive())
    return;
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
    draw_mines();
  }
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
    glColor4f( color[0], color[1], color[2], 0.3f );
    glCallList(force_shield);
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

void GLShip::draw_mines() const {
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

