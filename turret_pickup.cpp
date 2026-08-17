#include "turret_pickup.h"
#include "ship.h"
#include "gl_compat.h"

TurretPickup::TurretPickup(WrappedPoint pos) : Pickup(pos) {
  // The deployed drone in miniature: a ring with a barrel poking out.
  MeshBuilder mb;
  float r = radius * 0.55f;
  build_glow_icon(mb, 0.25f, 1.0f, 0.75f, [r](MeshBuilder& b, float s) {
    b.begin(GL_LINE_LOOP);
    for (int i = 0; i < 16; i++) {
      float a = i * 2.0f * (float)M_PI / 16.0f;
      b.vertex(cosf(a) * r * s, sinf(a) * r * s);
    }
    b.end();
    b.begin(GL_LINES);
    b.vertex(0.0f, 0.0f);
    b.vertex(1.7f * r * s, 0.0f);
    b.end();
  });
  glow_mesh.upload(mb);
}

void TurretPickup::apply(Ship *ship) {
  ship->add_turret_ammo(3);
}

void TurretPickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
