#include "typer.h"
#include "glstarfield.h"
#include "glgame.h"
#include "menu.h"
#include <GL/freeglut_std.h>
#include <GL/freeglut_ext.h>
#include <iostream>

const int Menu::default_world_width = 5000;
const int Menu::default_world_height = 5000;

Menu::Menu() :
  State(),
  currentTime(0),
  viewpoint(Point(0,default_world_height/2)),
  starfield(GLStarfield(Point(default_world_width,default_world_height))) {}

void Menu::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(90, window.x()/window.y(), 100.0f, 2000.0f);
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(0, 0, window.x(), window.y());

  glTranslatef(-viewpoint.x(), -viewpoint.y(), 0.0f);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);
  glTranslatef(default_world_width, 0, 0.0f);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);
  glTranslatef(-default_world_width*2, 0, 0.0f);
  starfield.draw_front(viewpoint);
  starfield.draw_rear(viewpoint);
  glTranslatef(default_world_width, 0, 0.0f);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);

  Typer::draw_centered(viewpoint.x(), viewpoint.y()+200, "Newtonia", 75);
  if((currentTime/1400) % 2) {
    Typer::draw_centered(viewpoint.x(), viewpoint.y()-50, "press enter", 16);
  }
  Typer::draw_centered(viewpoint.x(), viewpoint.y()-420, "© 2008-2014 METONYM.US", 12, currentTime);
}

void Menu::tick(int delta) {
  currentTime += delta;
  viewpoint += Point(1,0) * (0.1 * delta);
  //FIX: Wrapping bug
  if(viewpoint.x() > default_world_width) {
      viewpoint += Point(-default_world_width,0);
  }
}

void Menu::controller(SDL_Event event) {
 if(event.type == SDL_CONTROLLERBUTTONDOWN) {
    if(event.jbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
      glutLeaveMainLoop();
    } else if(event.jbutton.button == SDL_CONTROLLER_BUTTON_START ||
              event.jbutton.button == SDL_CONTROLLER_BUTTON_A) {
      request_state_change(new GLGame());
    }
  }
}

void Menu::keyboard(unsigned char key, int x, int y) {
}

void Menu::keyboard_up (unsigned char key, int x, int y) {
  switch(key) {
  case 27:
    glutLeaveMainLoop();
    break;
  case 13:
    request_state_change(new GLGame());
    break;
  }
}
