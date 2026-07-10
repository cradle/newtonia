#include "shield_pickup.h"
#include "ship.h"
#include "gl_compat.h"

ShieldPickup::ShieldPickup(WrappedPoint pos) : Pickup(pos) {
  // Three bubble arcs around a core dot — the force-shield ring.
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_icon(mb, 0.8f, 0.2f, 1.0f, [s](MeshBuilder& b, float k) {
    float d = s * k;
    for (int arc = 0; arc < 3; arc++) {
      b.begin(GL_LINE_STRIP);
      for (int i = 0; i <= 6; i++) {
        float a = (arc * 120.0f + i * 80.0f / 6.0f) * (float)M_PI / 180.0f;
        b.vertex(cosf(a) * 0.95f * d, sinf(a) * 0.95f * d);
      }
      b.end();
    }
    b.begin(GL_LINE_LOOP);   // core
    for (int i = 0; i < 6; i++) {
      float a = i * 2.0f * (float)M_PI / 6.0f;
      b.vertex(cosf(a) * 0.16f * d, sinf(a) * 0.16f * d);
    }
    b.end();
  });
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
