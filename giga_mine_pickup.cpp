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
  ship->add_giga_mine_ammo(10);
}

void GigaMinePickup::draw(float world_rotation) const {
  float s = radius * 0.8f;
  float outer = s;
  float inner = s * 0.4f;

  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glLineWidth(1.8f);
  glColor3f(0.6f, 0.0f, 1.0f);

  // 5-pointed star: 10 vertices alternating outer/inner
  glBegin(GL_LINE_LOOP);
  for(int i = 0; i < 10; i++) {
    float angle = i * M_PI / 5.0f - M_PI / 2.0f;
    float r = (i % 2 == 0) ? outer : inner;
    glVertex2f(cos(angle) * r, sin(angle) * r);
  }
  glEnd();
}
