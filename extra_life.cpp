#include "extra_life.h"
#include "ship.h"
#include "gl_compat.h"

ExtraLife::ExtraLife(WrappedPoint pos) : Pickup(pos) {
}

void ExtraLife::apply(Ship *ship) {
  ship->lives++;
}

void ExtraLife::draw(float world_rotation) const {
  float s = radius * 0.8f;

  glTranslatef(position.x(), position.y(), 0.0f);
  // Counter-rotate by the world rotation so the heart always appears upright
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  draw_glow_heart(1.0f, 0.0f, 0.0f, s);
}
