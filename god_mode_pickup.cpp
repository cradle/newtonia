#include "god_mode_pickup.h"
#include "asteroid.h"
#include "ship.h"
#include "gl_compat.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GodModePickup::GodModePickup(WrappedPoint pos) : Pickup(pos) {
}

void GodModePickup::apply(Ship * /*ship*/) {
  Asteroid::god_mode = true;
  Asteroid::god_mode_time_left = duration_ms;
}

void GodModePickup::draw(float world_rotation) const {
  float s = radius * 0.7f;

  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glLineWidth(2.0f);
  glColor3f(1.0f, 0.9f, 0.0f); // gold

  // Lightning bolt: 6-vertex polygon
  glBegin(GL_LINE_LOOP);
    glVertex2f( 0.2f * s,  1.0f * s);  // top
    glVertex2f(-0.2f * s,  0.1f * s);  // upper left
    glVertex2f( 0.3f * s,  0.1f * s);  // upper notch right
    glVertex2f(-0.2f * s, -1.0f * s);  // bottom
    glVertex2f( 0.2f * s, -0.1f * s);  // lower right
    glVertex2f(-0.3f * s, -0.1f * s);  // lower notch left
  glEnd();
}
