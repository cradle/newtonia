#include "mine_pickup.h"
#include "ship.h"
#include "gl_compat.h"

MinePickup::MinePickup(WrappedPoint pos) : Pickup(pos) {
  // The in-game mine glyph: rotated diamond with a cross inside.
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_icon(mb, 1.0f, 0.5f, 0.0f, [s](MeshBuilder& b, float k) {
    float d = s * k;
    b.begin(GL_LINE_LOOP);
    b.vertex(0, d); b.vertex(0.9f * d, 0); b.vertex(0, -d); b.vertex(-0.9f * d, 0);
    b.end();
    b.begin(GL_LINES);
    b.vertex(-0.45f * d, 0); b.vertex(0.45f * d, 0);
    b.vertex(0, -0.5f * d); b.vertex(0, 0.5f * d);
    b.end();
  });
  glow_mesh.upload(mb);
}

void MinePickup::apply(Ship *ship) {
  ship->add_mine_ammo(10);
}

void MinePickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
