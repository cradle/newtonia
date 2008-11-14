#include "game.h"
#include "glship.h"
		
#include <GLUT/glut.h>

Game::Game() {
  ship = GLShip(0,0);
  window_width = 800;
  window_height = 600;
}
  
Game::Game(float width, float height) {
  ship = GLShip(0,0);
  ship.resize(width, height);
  window_width = width;
  window_height = height;
}

void Game::tick(void) {
  int current_time = glutGet(GLUT_ELAPSED_TIME); 
  ship.step(current_time - last_tick);
  last_tick = current_time;
  glutPostRedisplay();
}

void Game::resize(int width, int height) { 
  window_width = width;
  window_height = height;

  ship.resize(width, height);

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
  ship.draw();
  glutSwapBuffers();
}

void Game::keyboard (unsigned char key, int x, int y) {
  ship.input(key);
}
void Game::keyboard_up (unsigned char key, int x, int y) {
  ship.input(key, false);
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