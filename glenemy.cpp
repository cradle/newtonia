#include "glenemy.h"
#include "gltrail.h"
#include "enemy.h"

#include "gl_compat.h"
#include "mesh.h"

#include "follower.h"
#include <list>

using namespace std;

GLEnemy::GLEnemy(const Grid &grid, float x, float y, list<GLShip*>* targets, float difficulty, list<Object*>* asteroids) : GLShip(grid, false) {
  list<Ship*>* ships = new list<Ship*>;
  list<GLShip*>::iterator s;
  for(s = targets->begin(); s != targets->end(); s++) {
    ships->push_back((*s)->ship);
  }
  ship = new Ship(grid); // FIX: Enemy is unused
  ship->behaviours.push_back(new Follower(ship, (list<Object*>*)ships, asteroids, difficulty));
  ship->position = WrappedPoint(x,y);
  ship->thrust_force = 0.129 + difficulty*0.00025 + rand()%50/10000.0;
  ship->rotation_force = 0.15 + difficulty*0.01 + rand()%10/1000.0;
  ship->value = 50 + difficulty * 50;
  ship->lives = 1;

  trails.push_back(new GLTrail(this, 0.05));

  color[0] = color[2] = 0.0;
  color[1] = 255/255.0;

  {
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex( 0.0f, 1.0f); mb.vertex(-0.8f,-0.9f);
    mb.vertex(-0.0f,-1.3f); mb.vertex( 0.8f,-0.9f);
    mb.end();
    body_fill.upload(mb);

    mb.clear();
    mb.begin(GL_LINE_LOOP);
    mb.color(color[0], color[1], color[2]);
    mb.vertex( 0.0f, 1.0f); mb.vertex(-0.8f,-0.9f);
    mb.vertex(-0.0f,-1.3f); mb.vertex( 0.8f,-0.9f);
    mb.end();
    body_outline.upload(mb);
    // jets mesh stays empty — enemy has no thruster effect
  }

  genForceShield();
}

GLEnemy::~GLEnemy() {
}
