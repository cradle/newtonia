#include "state.h"

#include "typer.h"
#include "glstarfield.h"

#include <iostream>

State::State() : starfield(GLStarfield(Point(10000,10000))) {
}

void State::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport(0, 0, window.x(), window.y());
  
  starfield.draw_rear(viewpoint);
  
  typer.draw(-50*7,150,"Newtonia",50);
  typer.draw(-30*7,0,"1 - Solo",30);
  typer.draw(-30*7,-100,"2 - Duet",30);

  starfield.draw_front(viewpoint);
}

void State::tick(int delta) {
  viewpoint += Point(1,0) * (0.1 * delta);
}

void State::resize(int x, int y) {
  window = Point(x, y);
}

void State::keyboard(unsigned char key, int x, int y) {}

void State::keyboard_up (unsigned char key, int x, int y) {
  if (key == '1') std::cout << "1 player" << std::endl;
  if (key == '2') std::cout << "2 players" << std::endl;
}