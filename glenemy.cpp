#include "glenemy.h"
#include "gltrail.h"
#include "enemy.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <list>

using namespace std;

GLEnemy::GLEnemy(float x, float y, list<GLShip*>* targets, float difficulty) : GLShip(NULL) {
  list<Ship*>* ships = new list<Ship*>;
  list<GLShip*>::iterator s;
  for(s = targets->begin(); s != targets->end(); s++) {
    ships->push_back((*s)->ship);
  }
  ship = new Enemy(x,y, ships, difficulty);
  trails.push_back(new GLTrail(ship, 0.05));
  
  color[0] = color[2] = 0.0;
  color[1] = 255/255.0;
  
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

GLEnemy::~GLEnemy() {
  glDeleteLists(body, 1);
  glDeleteLists(jets, 1);
}