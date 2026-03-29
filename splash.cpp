#include "splash.h"
#include "menu.h"
#include "typer.h"
#include "gl_compat.h"

Splash::Splash() : State() {}

void Splash::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glViewport(0, 0, window.x(), window.y());

  Typer::draw_centered(0, 0, "Newtonia", 80);
}

void Splash::tick(int delta) {}

void Splash::keyboard(unsigned char key, int x, int y) {}

void Splash::keyboard_up(unsigned char key, int x, int y) {
  request_state_change(new Menu());
}

void Splash::controller(SDL_Event event) {
  if (event.type == SDL_CONTROLLERBUTTONDOWN)
    request_state_change(new Menu());
}
