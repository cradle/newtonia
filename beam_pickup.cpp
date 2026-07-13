#include "beam_pickup.h"
#include "ship.h"
#include "gl_compat.h"

BeamPickup::BeamPickup(WrappedPoint pos) : Pickup(pos) {
  // A long diagonal bolt: pointed head, trailing dashes.
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_icon(mb, 0.7f, 0.4f, 1.0f, [s](MeshBuilder& b, float k) {
    float d = s * k;
    b.begin(GL_LINES);
    b.vertex(-0.7f * d, -0.5f * d); b.vertex(0.85f * d, 0.6f * d);  // bolt
    b.vertex(0.85f * d, 0.6f * d);  b.vertex(0.5f * d, 0.55f * d);  // head
    b.vertex(0.85f * d, 0.6f * d);  b.vertex(0.68f * d, 0.28f * d);
    b.vertex(-0.95f * d, -0.68f * d); b.vertex(-0.82f * d, -0.59f * d);  // dash
    b.end();
  });
  glow_mesh.upload(mb);
}

void BeamPickup::apply(Ship *ship) {
  ship->add_beam_ammo(100);
}

void BeamPickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
