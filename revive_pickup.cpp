#include "revive_pickup.h"
#include "ship.h"
#include "gl_compat.h"

RevivePickup::RevivePickup(WrappedPoint pos) : Pickup(pos) {
  // Glowing green medic cross, layered like the star/heart helpers.
  float s = radius * 0.8f;
  struct Layer { float scale; float alpha; };
  static const Layer layers[] = {
    {2.0f,  0.05f},
    {1.5f,  0.12f},
    {1.15f, 0.28f},
    {1.0f,  1.0f },
  };
  MeshBuilder mb;
  for (const Layer& L : layers) {
    float a = 0.35f * s * L.scale;  // arm half-width
    float b = 1.0f * s * L.scale;   // arm length
    mb.begin(GL_LINE_LOOP);
    mb.color(0.2f, 1.0f, 0.4f, L.alpha);
    mb.vertex(-a,  b); mb.vertex( a,  b);   // top arm
    mb.vertex( a,  a); mb.vertex( b,  a);   // right arm
    mb.vertex( b, -a); mb.vertex( a, -a);
    mb.vertex( a, -b); mb.vertex(-a, -b);   // bottom arm
    mb.vertex(-a, -a); mb.vertex(-b, -a);   // left arm
    mb.vertex(-b,  a); mb.vertex(-a,  a);
    mb.end();
  }
  glow_mesh.upload(mb);
}

// The revive targets the fallen PARTNER, not the collector — GLGame applies
// it at the collection site, where the player list lives (same pattern as
// the mini-station reward). Nothing to do to the collector here.
void RevivePickup::apply(Ship *ship) {
  (void)ship;
}

void RevivePickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  // Counter-rotate so the cross always appears upright.
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
