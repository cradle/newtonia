#include "beam_pickup.h"
#include "ship.h"
#include "gl_compat.h"

BeamPickup::BeamPickup(WrappedPoint pos) : Pickup(pos) {
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_star(mb, 0.7f, 0.4f, 1.0f, s, s * 0.4f);
  glow_mesh.upload(mb);
}

void BeamPickup::apply(Ship *ship) {
  ship->add_beam_ammo(20);
}

void BeamPickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
