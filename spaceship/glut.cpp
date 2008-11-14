 #include <stdlib.h> // For EXIT_SUCCESS

#include "glgame.h"

#include <GLUT/glut.h>

#include <iostream>

GLGame glgame;

void draw() {
  glgame.draw();
}

void keyboard(unsigned char key, int x, int y) {
  if (key == 27) // ESC
    exit(0);
  glgame.keyboard(key, x, y);
}

void keyboard_up(unsigned char key, int x, int y) {
  glgame.keyboard_up(key, x, y);
}

void resize(int width, int height) {
  glgame.resize(width, height);
}

void tick() {
  glgame.tick();
}

void isVisible(int state) {
  if(state == GLUT_VISIBLE) {
    glutVisibilityFunc(NULL);
    glutIdleFunc(tick);
  }
}

int main(int argc, char** argv) {  
  glgame = GLGame(800,600);
  glgame.init(argc, argv);
  
  glutDisplayFunc(draw); 
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboard_up);
  glutReshapeFunc(resize);
  glutVisibilityFunc(isVisible);
  //TODO: stop using glut, use SDL
  
  glgame.run();
   
  return EXIT_SUCCESS;
}