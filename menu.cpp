#include "menu.h"

#include "typer.h"
#include "glstarfield.h"
#include "glgame.h"

#include <iostream>

Menu::Menu() : 
  State(), 
  currentTime(0),
  viewpoint(Point(-10000,0)),
  selected(ARCADE),
  starfield(GLStarfield(Point(10000,10000))) {}

void Menu::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(0, 0, window.x(), window.y());
  
  starfield.draw_rear(viewpoint);
  
   if((currentTime/700) % 2) {
     Typer::draw_centered(0, window.y() - 10, "press enter to start", 10); 
   }
  
  Typer::draw_centered(0, 400, "Newtonia", 75);
  
  Typer::draw_centered(0, 130, "arcade", 25);
  Typer::draw_centered(0, -20, "versus", 25);
  Typer::draw_centered(0, selected == ARCADE ? 130 : -20, ">        <", 25);
  
  Typer::draw_centered(0,-260,"a",10);
  Typer::draw_centered(0, -300, "Glenn Francis Murray", 15);
  Typer::draw_centered(0,-350,"production",10);
  Typer::draw_centered(0, -420, "Copyright 2008-2009", 10);

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
    switch(selected) {
    case ARCADE:
      request_state_change(new GLGame());
      break;
    case VERSUS:
      break;
    }
    break;
  case 'w':
  case 's':
    switch(selected) {
    case ARCADE:
      selected = VERSUS;
      break;
    case VERSUS:
      selected = ARCADE;
      break;
    }
    break;
  }
}