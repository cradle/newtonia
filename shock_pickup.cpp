#include "shock_pickup.h"
#include "ship.h"
#include "gl_compat.h"

ShockPickup::ShockPickup(WrappedPoint pos) : Pickup(pos) {
  float s = radius * 0.8f;
  // Electric blue-white glow star.
  MeshBuilder mb;
  build_glow_star(mb, 0.6f, 0.85f, 1.0f, s, s * 0.4f);
  glow_mesh.upload(mb);
}

void ShockPickup::apply(Ship *ship) {
  ship->add_shock_ammo(8);
}

void ShockPickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
