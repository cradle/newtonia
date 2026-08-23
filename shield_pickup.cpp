#include "shield_pickup.h"
#include "ship.h"
#include "gl_compat.h"

ShieldPickup::ShieldPickup(WrappedPoint pos) : Pickup(pos) {
  // Point-down shield outline — the same glyph the touch OSD's weapon
  // icons use (view/overlay.cpp draw_weapon_glyph), in the same cyan.
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_icon(mb, 0.4f, 0.9f, 1.0f, [s](MeshBuilder& b, float k) {
    float d = s * k;
    b.begin(GL_LINE_LOOP);
    b.vertex(-0.8f * d,  0.8f * d);
    b.vertex( 0.8f * d,  0.8f * d);
    b.vertex( 0.8f * d, -0.1f * d);
    b.vertex( 0.0f * d, -1.0f * d);
    b.vertex(-0.8f * d, -0.1f * d);
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
