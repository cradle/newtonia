#include "mine_pickup.h"
#include "ship.h"
#include "gl_compat.h"

MinePickup::MinePickup(WrappedPoint pos) : Pickup(pos) {
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_star(mb, 1.0f, 0.5f, 0.0f, s, s * 0.4f);
  glow_mesh.upload(mb);
}

void MinePickup::apply(Ship *ship) {
  ship->add_mine_ammo(10);
}

void MinePickup::draw(float world_rotation) const {
  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw();
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
