#include "glgame.h"
#include "glship.h"
#include "glenemy.h"
#include "glcar.h"
#include "glstarfield.h"
#include "wrapped_point.h"
#include "glstation.h"
#include "menu.h"
#include "state.h"
#include "world.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <iostream>
#include <list>

GLGame::GLGame(float width, float height, int player_count) : 
  State(), 
  running(true), 
  game_world(World(width, height, player_count)) {
}

GLGame::~GLGame() {}

void GLGame::tick(int delta) {
  game_world.tick(delta);
}

void GLGame::draw(void) {
  glClear(GL_COLOR_BUFFER_BIT);

  int width_scale = is_single() ? 1 : 2;
  glViewport((primary ? 0 : (window.x()/2)), 0, window.x()/width_scale, window.y());
  game_world.draw(players->front(), true); // we need access to players when we add them
  if (!game_world.is_single()) {
    glViewport((primary ? 0 : (window.x()/2)), 0, window.x()/width_scale, window.y());
    game_world.draw(players->back(), false);
  } 
  draw_map();
}

void GLGame::draw_map() const {
  float minimap_size = window.y()/4;

  if(!is_single()) {
    /* DRAW CENTER LINE */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, window.x(), window.y());
    glBegin(GL_LINES);
    glColor4f(1,1,1,0.5);
    glVertex2f(0,-window.y());
    glVertex2f(0,-minimap_size);
    glVertex2f(0, minimap_size);
    glVertex2f(0, window.y());
    glEnd();
  }

  /* MINIMAP */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-world.x(), world.x(), -world.y(), world.y());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  if (is_single()) {
    glViewport(window.x()/2 - minimap_size/2, 0, minimap_size, minimap_size);
  } else {
    glViewport(window.x()/2 - minimap_size/2, window.y()/2 - minimap_size/2, minimap_size, minimap_size);
  }
  glColor4f(0.0f,0.0f,0.0f,0.8f);
  /* BLACK BOX OVER MINIMAP */
  glBegin(GL_POLYGON);
    glVertex2i( -world.x(), world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(),-world.y());
    glVertex2i( -world.x(),-world.y());
  glEnd();
  /* LINE AROUND MINIMAP */
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINE_LOOP);
    glVertex2i( -world.x(), world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(),-world.y());
    glVertex2i( -world.x(),-world.y());
  glEnd();
  glPushMatrix();
  game_world.draw(true);
  glPopMatrix();

  /* DRAW THE LEVEL */
  Typer::draw(world.x()-1500, -world.y()+2000, station->level(), 800);
}

void GLGame::keyboard (unsigned char key, int x, int y) {
  game_state.keyboard(key, x, y);
}
void GLGame::keyboard_up(unsigned char key, int x, int y) {
  if (key == 27) request_state_change(new Menu());
  game_world.keyboard_up(key, x, y);
}