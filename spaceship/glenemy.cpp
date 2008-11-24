#include "glenemy.h"
#include "gltrail.h"
#include "enemy.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <windows.h>
#include <GL/glut.h>
#endif

using namespace std;

GLEnemy::GLEnemy(float x, float y, GLShip* target) {
  ship = new Enemy(x,y, target->ship);
  trails.push_back(new GLTrail(ship, GLTrail::DOTS, 0.3, 0.5));
  
  color[0] = color[2] = 0.0;
  color[1] = 1.0;
  
  body = glGenLists(1);
  glNewList(body, GL_COMPILE);
  glVertex2f( 0.0f, 1.0f);
  glVertex2f(-0.8f,-0.9f);
  glVertex2f(-0.0f,-1.3f);
  glVertex2f( 0.8f,-0.9);
  glEndList();
  
  jets = glGenLists(1);
  glNewList(jets, GL_COMPILE);
  glEndList();
}