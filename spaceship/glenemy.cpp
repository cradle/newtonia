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

#include <vector>

using namespace std;

GLEnemy::GLEnemy(float x, float y, vector<GLShip*>* targets) {
  vector<Ship*>* ships = new vector<Ship*>;
  for(unsigned int i = 0; i < targets->size(); i++) {
    ships->push_back((*targets)[i]->ship);
  }
  ship = new Enemy(x,y, ships);
  trails.push_back(new GLTrail(ship, 0.3));
  
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
