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
  title_text(glgame);

  if(glship != NULL) {
    score(glgame, glship);
    level_cleared(glgame);
    lives(glgame, glship);
    weapons(glgame, glship);
    temperature(glgame, glship);
    respawn_timer(glship);
  }
}

void Overlay::score(const GLGame *glgame, GLShip *glship) {
  //FIX: Window encapsulation? Players size encapsulation?
  Typer::draw(glgame->window.x()/glgame->players->size()-40, glgame->window.y()-20, glship->ship->score, 20);
  if(glship->ship->multiplier() > 1) {
    Typer::draw(glgame->window.x()/glgame->players->size()-35, glgame->window.y()-92, "x", 15);
    Typer::draw(glgame->window.x()/glgame->players->size()-65, glgame->window.y()-80, glship->ship->multiplier(), 20);
  }  
}

void Overlay::level_cleared(const GLGame *glgame) {
  if(glgame->level_cleared) {
    Typer::draw_centered(0, 150, "CLEARED", 50);
    Typer::draw_centered(0, -60, (glgame->time_until_next_generation / 1000), 20);
  }
}

void Overlay::lives(const GLGame *glgame, GLShip *glship) {
  Typer::draw_lives(glgame->window.x()/glgame->players->size()-40, -glgame->window.y()+70, glship, 18);
}

void Overlay::weapons(const GLGame *glgame, GLShip *glship) {
  glPushMatrix();
  glTranslatef(-glgame->window.x()/glgame->players->size()+10, glgame->window.y()-10, 0.0f);
  glship->draw_weapons();
  glPopMatrix();
}

void Overlay::temperature(const GLGame *glgame, GLShip *glship) {
  glPushMatrix();
  glTranslatef(-glgame->window.x()/glgame->players->size()+30, -glgame->window.y()+15, 0.0f);
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

void Overlay::title_text(const GLGame *glgame) {
  if(glgame->players->size() < 2) {
    Typer::draw_centered(0, glgame->window.y()-20, "press enter to join", 8);
  } else {
    if(glgame->friendly_fire) {
      Typer::draw_centered(0, glgame->window.y()-20, "friendly fire on", 8);
    }
  }
}
