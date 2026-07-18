#include "giga_mine_pickup.h"
#include "ship.h"
#include "gl_compat.h"

GigaMinePickup::GigaMinePickup(WrappedPoint pos) : Pickup(pos) {
  // The mine glyph ringed by a blast circle — the big one.
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_icon(mb, 0.6f, 0.0f, 1.0f, [s](MeshBuilder& b, float k) {
    float d = s * k * 0.72f;
    b.begin(GL_LINE_LOOP);
    b.vertex(0, d); b.vertex(0.9f * d, 0); b.vertex(0, -d); b.vertex(-0.9f * d, 0);
    b.end();
    b.begin(GL_LINES);
    b.vertex(-0.45f * d, 0); b.vertex(0.45f * d, 0);
    b.vertex(0, -0.5f * d); b.vertex(0, 0.5f * d);
    b.end();
    b.begin(GL_LINE_LOOP);
    for (int i = 0; i < 12; i++) {
      float a = i * 2.0f * (float)M_PI / 12.0f;
      b.vertex(cosf(a) * s * k * 1.05f, sinf(a) * s * k * 1.05f);
    }
    b.end();
  });
  glow_mesh.upload(mb);
}

void GigaMinePickup::apply(Ship *ship) {
  ship->add_giga_mine_ammo(10);
}

void GigaMinePickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
