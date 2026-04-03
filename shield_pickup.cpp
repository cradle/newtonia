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
  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw();
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
