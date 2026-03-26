#include "missile_pickup.h"
#include "ship.h"
#include "gl_compat.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MissilePickup::MissilePickup(WrappedPoint pos) : Pickup(pos) {
}

void MissilePickup::apply(Ship *ship) {
  ship->add_missile_ammo(10);
}

void MissilePickup::draw(float world_rotation) const {
  float s = radius * 0.8f;
  float outer = s;
  float inner = s * 0.4f;

  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  draw_glow_star(0.2f, 0.8f, 1.0f, outer, inner);
}
