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
  glTranslatef(position.x(), position.y(), 0.0f);
  // Counter-rotate by the world rotation so the heart always appears upright
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw();
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
