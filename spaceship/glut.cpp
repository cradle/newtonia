#include <stdlib.h> // For EXIT_SUCCESS

#include "game.h"

#include <GLUT/glut.h>

#include <iostream>

Game game;

void draw() {
  game.draw();
}

void keyboard(unsigned char key, int x, int y) {
  if (key == 27) // ESC
    exit(0);
  game.keyboard(key, x, y);
}

void keyboard_up(unsigned char key, int x, int y) {
  game.keyboard_up(key, x, y);
}

void resize(int width, int height) {
  game.resize(width, height);
}

void tick() {
  game.tick();
}

void isVisible(int state) {
  if(state == GLUT_VISIBLE) {
    glutVisibilityFunc(NULL);
    glutIdleFunc(tick);
  }
}

int main(int argc, char** argv) {  
  game = Game(640,480);
  game.init(argc, argv);
  
  glutDisplayFunc(draw);
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutReshapeFunc(resize);
  glutVisibilityFunc(isVisible);
  //TODO: stop using glut, use SDL
  
  game.run();
   
  return EXIT_SUCCESS;
}