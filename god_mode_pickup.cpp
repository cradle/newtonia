#include "god_mode_pickup.h"
#include "ship.h"
#include "gl_compat.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GodModePickup::GodModePickup(WrappedPoint pos) : Pickup(pos) {
}

void GodModePickup::apply(Ship *ship) {
  ship->add_god_mode();
}

void GodModePickup::draw(float world_rotation) const {
  float s = radius * 0.8f;
  float outer = s;
  float inner = s * 0.4f;

  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  // Gold color to distinguish from other pickups
  draw_glow_star(1.0f, 0.9f, 0.0f, outer, inner);
}
