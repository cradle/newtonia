#include "giga_mine_pickup.h"
#include "ship.h"
#include "gl_compat.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GigaMinePickup::GigaMinePickup(WrappedPoint pos) : Pickup(pos) {
}

void GigaMinePickup::apply(Ship *ship) {
  ship->add_giga_mine_ammo(1);
}

void GigaMinePickup::draw(float world_rotation) const {
  float s = radius * 1.1f;
  float outer = s;
  float inner = s * 0.35f;
  float mid   = s * 0.65f;

  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);

  // Outer 8-pointed star (larger, red/orange)
  glLineWidth(2.5f);
  glColor3f(1.0f, 0.2f, 0.0f);
  glBegin(GL_LINE_LOOP);
  for(int i = 0; i < 16; i++) {
    float angle = i * M_PI / 8.0f - M_PI / 2.0f;
    float r = (i % 2 == 0) ? outer : inner;
    glVertex2f(cos(angle) * r, sin(angle) * r);
  }
  glEnd();

  // Inner ring to make it look more menacing
  glLineWidth(1.5f);
  glColor3f(1.0f, 0.5f, 0.0f);
  glBegin(GL_LINE_LOOP);
  for(int i = 0; i < 12; i++) {
    float angle = i * 2.0f * M_PI / 12.0f;
    glVertex2f(cos(angle) * mid * 0.4f, sin(angle) * mid * 0.4f);
  }
  glEnd();

  // Center cross
  glBegin(GL_LINES);
  glVertex2f(-inner * 0.5f, 0.0f);
  glVertex2f( inner * 0.5f, 0.0f);
  glVertex2f(0.0f, -inner * 0.5f);
  glVertex2f(0.0f,  inner * 0.5f);
  glEnd();
}
