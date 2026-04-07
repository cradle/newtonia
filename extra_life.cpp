#include "extra_life.h"
#include "ship.h"
#include "gl_compat.h"

ExtraLife::ExtraLife(WrappedPoint pos) : Pickup(pos) {
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_heart(mb, 1.0f, 0.0f, 0.0f, s);
  glow_mesh.upload(mb);
}

void ExtraLife::apply(Ship *ship) {
  ship->lives++;
}

void ExtraLife::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  // Counter-rotate by the world rotation so the heart always appears upright
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
