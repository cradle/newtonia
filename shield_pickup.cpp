#include "shield_pickup.h"
#include "ship.h"
#include "gl_compat.h"

ShieldPickup::ShieldPickup(WrappedPoint pos) : Pickup(pos) {
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_star(mb, 0.8f, 0.2f, 1.0f, s, s * 0.4f);
  glow_mesh.upload(mb);
}

void ShieldPickup::apply(Ship *ship) {
  ship->add_shield_ammo(10);
}

void ShieldPickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
