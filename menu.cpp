#include "menu.h"

#include "typer.h"
#include "glstarfield.h"
#include "glgame.h"

#include <iostream>

Menu::Menu() : 
  State(), 
  currentTime(0),
  viewpoint(Point(-10000,0)),
  starfield(GLStarfield(Point(5000,5000))) {}

void Menu::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(0, 0, window.x(), window.y());
  
  starfield.draw_rear(viewpoint);
  
  Typer::draw_centered(0, 200, "Newtonia", 75);
  Typer::draw_centered(0,-260,"a",10);
  Typer::draw_centered(0, -300, "Glenn Francis Murray", 15);
  if((currentTime/700) % 2) {
    Typer::draw_centered(0, -50, "press enter to start", 15); 
  }
  Typer::draw_centered(0,-350,"production",10);
  Typer::draw_centered(0, -420, "Copyright 2008-2010", 10);

  starfield.draw_front(viewpoint);
}

void Menu::tick(int delta) {
  currentTime += delta;
  viewpoint += Point(1,0) * (0.1 * delta);
  //FIX: Wrapping bug
  // viewpoint.wrap();
}

void Menu::keyboard(unsigned char key, int x, int y) {
  if(key == 27) exit(0); // escape
}

void Menu::keyboard_up (unsigned char key, int x, int y) {
  switch(key) {
  case 13:
    request_state_change(new GLGame());
    break;
  }
}