#include "glcar.h"
#include "gltrail.h"

#include "gl_compat.h"
#include "mesh.h"

#include <iostream>

using namespace std;

GLCar::GLCar(const Grid &grid, bool has_friction) : GLShip(grid, has_friction) {
  ship = new Ship(grid, has_friction);
  trails.clear();
  trails.push_back(new GLTrail(this, 0.01, Point( 4.5,0),0.25, 0.0, GLTrail::THRUSTING | GLTrail::LEFT, 1000.0));
  trails.push_back(new GLTrail(this, 0.01, Point(-4.5,0),0.25, 0.0, GLTrail::THRUSTING | GLTrail::RIGHT, 1000.0));
  trails.push_back(new GLTrail(this, 0.5,  Point(-4,17) ,-0.2, 0.9, GLTrail::REVERSING | GLTrail::RIGHT, 250.0));
  trails.push_back(new GLTrail(this, 0.5,  Point( 4,17) ,-0.2,-0.9, GLTrail::REVERSING | GLTrail::LEFT, 250.0));

  color[0] = 255/255.0;
  color[1] = 69/255.0;
  color[2] = 0/255.0;

  {
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex( 0.35f, 1.0f); mb.vertex(-0.35f, 1.0f);
    mb.vertex(-0.8f, -1.0f); mb.vertex( 0.8f,  -1.0f);
    mb.end();
    body_fill.upload(mb);

    mb.clear();
    mb.begin(GL_LINE_LOOP);
    mb.color(color[0], color[1], color[2]);
    mb.vertex( 0.35f, 1.0f); mb.vertex(-0.35f, 1.0f);
    mb.vertex(-0.8f, -1.0f); mb.vertex( 0.8f,  -1.0f);
    mb.end();
    body_outline.upload(mb);
  }

  {
    float rc = 1-color[0], gc = 1-color[1], bc = 1-color[2];
    MeshBuilder mb;
    mb.begin(GL_TRIANGLES);
    mb.color(rc, gc, bc);
    mb.vertex( 0.8f,-1.0f); mb.vertex( 0.4f,-1.75f); mb.vertex( 0.0f,-1.0f);
    mb.end();
    left_jet.upload(mb);

    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(rc, gc, bc);
    mb.vertex( 0.0f,-1.0f); mb.vertex(-0.4f,-1.75f); mb.vertex(-0.8f,-1.0f);
    mb.end();
    right_jet.upload(mb);

    // combined jets mesh (both left and right)
    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(rc, gc, bc);
    mb.vertex( 0.8f,-1.0f); mb.vertex( 0.4f,-1.75f); mb.vertex( 0.0f,-1.0f);
    mb.vertex( 0.0f,-1.0f); mb.vertex(-0.4f,-1.75f); mb.vertex(-0.8f,-1.0f);
    mb.end();
    jets.upload(mb);
  }

  genForceShield();
  genRepulsor();
}

GLCar::~GLCar() {
}

void GLCar::draw_ship(bool minimap) const {
  GLShip::draw_ship(minimap);

  if(!minimap) {
    if(ship->rotation_direction == Ship::LEFT) {
      left_jet.draw();
    } else if (ship->rotation_direction == Ship::RIGHT) {
      right_jet.draw();
    }
  }
}
