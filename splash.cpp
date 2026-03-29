#include "splash.h"
#include "menu.h"
#include "typer.h"
#include "gl_compat.h"

// Fixed ortho extents so text always fits within 1280x720
const float Splash::ORTHO_W = 1280.0f;
const float Splash::ORTHO_H = 720.0f;

Splash::Splash() : State() {}

void Splash::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-ORTHO_W, ORTHO_W, -ORTHO_H, ORTHO_H);
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
