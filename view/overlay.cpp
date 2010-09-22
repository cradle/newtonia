#include "overlay.h"
#include "glship.h"
#include "glgame.h"
#include "typer.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

void Overlay::draw(const GLGame *glgame, GLShip *glship) {
  title_text();

  if(glship != NULL) {
    score(glship);
    if(glgame->level_cleared) {
      level_cleared();
    }
    lives(glship);
    weapons(glship);
    temperature(glship);
    respawn_timer(glship);
  }
}

void Overlay::score(GLShip *glship) {
  Typer::draw(window.x()/width_scale-40, window.y()-20, glship->ship->score, 20);
  if(glship->ship->multiplier() > 1) {
    Typer::draw(window.x()/width_scale-35, window.y()-92, "x", 15);
    Typer::draw(window.x()/width_scale-65, window.y()-80, glship->ship->multiplier(), 20);
  }  
}

void Overlay::level_cleared() {
  Typer::draw_centered(0, 150, "CLEARED", 50);
  Typer::draw_centered(0, -60, (time_until_next_generation / 1000), 20);
}

void Overlay::lives(GLShip *glship) {
  Typer::lives(window.x()/width_scale-40, -window.y()+70, glship, 18);
}

void Overlay::weapons(GLShip *glship) {
  glPushMatrix();
  glTranslatef(-window.x()/width_scale+10, window.y()-10, 0.0f);
  glship->draw_weapons();
  glPopMatrix();
}

void Overlay::temperature(GLShip *glship) {
  glPushMatrix();
  glTranslatef(-window.x()/width_scale+30, -window.y()+15, 0.0f);
  glPushMatrix();
  glScalef(30,30,1);
  glship->draw_temperature();
  glPopMatrix();
  glTranslatef(42.0f, 147.0f, 0.0f);
  glScalef(10,10,1);
  glship->draw_temperature_status();
  glPopMatrix();
}

void Overlay::respawn_timer(GLShip *glship) {
  glPushMatrix();
  glScalef(20,20,1);
  glship->draw_respawn_timer();
  glPopMatrix();
}

void Overlay::title_text(GLShip *glship) {
  if(players->size() < 2) {
    Typer::draw_centered(0, window.y()-20, "press enter to join", 8);
  } else {
    if(friendly_fire) {
      Typer::draw_centered(0, window.y()-20, "friendly fire on", 8);
    }
  }
}
