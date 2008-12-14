#include "menu.h"

#include "typer.h"
#include "glstarfield.h"
#include "glgame.h"

#include <iostream>

Menu::Menu() : State(), starfield(GLStarfield(Point(10000,10000))) {}

void Menu::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(0, 0, window.x(), window.y());
  
  starfield.draw_rear(viewpoint);
  
  typer.draw(-50*7,  350, "Newtonia", 50);
  typer.draw(-30*7,  150, "1 - Solo", 30);
  typer.draw(-30*7,   25, "2 - Duet", 30);
  typer.draw(-30*7, -100, "3 - Duel", 30);
  typer.draw(0,-260,"a",10);
  typer.draw(-15*18, -300, "Glenn Francis Murray", 15);
  typer.draw(-10*9,-350,"production",10);
  typer.draw(-10*13.5, -420, "Copyright 2008", 10);

  starfield.draw_front(viewpoint);
}

void Menu::tick(int delta) {
  viewpoint += Point(1,0) * (0.1 * delta);
}

void Menu::keyboard(unsigned char key, int x, int y) {
  if(key == 27) exit(0); // escape
}

void Menu::keyboard_up (unsigned char key, int x, int y) {
  if (key == '1') {
    request_state_change(new GLGame(10000, 10000, 1));
  } else if (key == '2') {
    request_state_change(new GLGame(10000, 10000, 2));  
  } else if (key == '3') {
    request_state_change(new GLGame(5000, 5000, 2, false)); 
  } else if (key == '4') {
    request_state_change(new GLGame(5000, 5000, 1, false));
  }
}