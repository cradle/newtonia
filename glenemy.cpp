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

#include "follower.h"
#include <list>

using namespace std;

GLEnemy::GLEnemy(const Grid &grid, float x, float y, list<GLShip*>* targets, float difficulty) : GLShip(grid, NULL) {
  list<Ship*>* ships = new list<Ship*>;
  list<GLShip*>::iterator s;
  for(s = targets->begin(); s != targets->end(); s++) {
    ships->push_back((*s)->ship);
  }
  ship = new Ship(grid); // FIX: Enemy is unused
  ship->behaviours.push_back(new Follower(ship, (list<Object*>*)ships));
  ship->position = WrappedPoint(x,y);
  ship->thrust_force = 0.129 + difficulty*0.00025 + rand()%50/10000.0;
  ship->rotation_force = 0.15 + difficulty*0.01 + rand()%10/1000.0;
  ship->value = 50 + difficulty * 50;
  ship->lives = 1;

  trails.push_back(new GLTrail(this, 0.05));

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

  genForceShield();
}

GLEnemy::~GLEnemy() {
  glDeleteLists(body, 1);
  glDeleteLists(jets, 1);
}
