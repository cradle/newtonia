#include "game.h"
#include "glship.h"

#include <GLUT/glut.h>
  
Game::Game(float width, float height) {
  player1 = GLShip(-width*3/4,-height*3/4);
  player1.resize(width, height);
  player2 = GLShip(width*3/4,height*3/4);
  player2.resize(width, height);
  player2.set_keys('j','l','i','/');
  window_width = width;
  window_height = height;
}

void Game::tick(void) {
  int current_time = glutGet(GLUT_ELAPSED_TIME); 
  player1.step(current_time - last_tick);
  player2.step(current_time - last_tick);
  last_tick = current_time;
  glutPostRedisplay();
}

void Game::resize(int width, int height) { 
  window_width = width;
  window_height = height;

  player1.resize(width, height);
  player2.resize(width, height);

  glViewport(0, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  //TODO: Refactor into 0, width, 0, height, and change code elsewhere
  glOrtho(-width/2, width/2, -height/2, height/2, -1, 1);

  glMatrixMode(GL_MODELVIEW);  
  glLoadIdentity();
}

void Game::draw(void) {  
  glClear(GL_COLOR_BUFFER_BIT);
  glLoadIdentity();
  player1.draw();
  player2.draw();
  glutSwapBuffers();
}

void Game::keyboard (unsigned char key, int x, int y) {
  player1.input(key);
  player2.input(key);
}
void Game::keyboard_up (unsigned char key, int x, int y) {
  player1.input(key, false);
  player2.input(key, false);
}

void Game::init(int argc, char** argv) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
  glutInitWindowSize(window_width, window_height);
  glutCreateWindow("Asteroids");
  
  glShadeModel(GL_FLAT);            
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);   
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);  
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

void Game::run(void) {
  last_tick = glutGet(GLUT_ELAPSED_TIME);
  glutMainLoop();
}