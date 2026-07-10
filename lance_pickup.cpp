#include "lance_pickup.h"
#include "ship.h"
#include "gl_compat.h"

LancePickup::LancePickup(WrappedPoint pos) : Pickup(pos) {
  // A full-width double-ended arrow — goes all the way through.
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_icon(mb, 1.0f, 0.8f, 0.2f, [s](MeshBuilder& b, float k) {
    float d = s * k;
    b.begin(GL_LINES);
    b.vertex(-d, 0); b.vertex(d, 0);
    b.vertex(d, 0);  b.vertex(0.68f * d, 0.24f * d);
    b.vertex(d, 0);  b.vertex(0.68f * d, -0.24f * d);
    b.vertex(-d, 0); b.vertex(-0.68f * d, 0.24f * d);
    b.vertex(-d, 0); b.vertex(-0.68f * d, -0.24f * d);
    b.end();
  });
  glow_mesh.upload(mb);
}

void LancePickup::apply(Ship *ship) {
  ship->add_lance_ammo(10);
}

void LancePickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
