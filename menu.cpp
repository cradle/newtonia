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
  starfield(GLStarfield(Point(default_world_width,default_world_height))) {
  if(music == NULL) {
    music = Mix_LoadMUS("title.mp3");
    if(music == NULL) {
      std::cout << "Unable to load title.mp3 (" << Mix_GetError() << ")" << std::endl;
    } else {
      Mix_PlayMusic(music, -1);
    }
  }
}

Menu::~Menu() {
  Mix_FreeMusic(music);
}

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

  glTranslatef(viewpoint.x(), viewpoint.y(), 0.0f);
  Typer::draw_centered(0, 200, "Newtonia", 80);
  if((currentTime/1400) % 2) {
    if(SDL_NumJoysticks() == 0) {
      Typer::draw_centered(0, -50, "press enter", 18);
    } else {
      Typer::draw_centered(0, -50, "press start", 18);
    }
  }
  Typer::draw_centered(0, -420, "© 2008-2014", 13, currentTime);
}

void Menu::tick(int delta) {
  currentTime += delta;
  viewpoint += Point(1,0) * (0.025 * delta);
  //FIX: Wrapping bug
  if(viewpoint.x() > default_world_width) {
      viewpoint += Point(-default_world_width,0);
  }
}

void Menu::controller(SDL_Event event) {
 if(event.type == SDL_CONTROLLERBUTTONDOWN) {
    if(event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
      glutLeaveMainLoop();
    } else if(event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
      request_state_change(new GLGame(SDL_GameControllerOpen(event.cbutton.which)));
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
