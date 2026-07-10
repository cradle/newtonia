#include "missile_pickup.h"
#include "ship.h"
#include "gl_compat.h"

MissilePickup::MissilePickup(WrappedPoint pos) : Pickup(pos) {
  // A finned rocket, nose up, exhaust dash below.
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_icon(mb, 0.2f, 0.8f, 1.0f, [s](MeshBuilder& b, float k) {
    float d = s * k;
    b.begin(GL_LINE_LOOP);   // nose + body
    b.vertex(0, d);
    b.vertex(0.3f * d, 0.45f * d);  b.vertex(0.3f * d, -0.55f * d);
    b.vertex(-0.3f * d, -0.55f * d); b.vertex(-0.3f * d, 0.45f * d);
    b.end();
    b.begin(GL_LINE_STRIP);  // left fin
    b.vertex(-0.3f * d, -0.15f * d); b.vertex(-0.75f * d, -0.75f * d);
    b.vertex(-0.3f * d, -0.55f * d);
    b.end();
    b.begin(GL_LINE_STRIP);  // right fin
    b.vertex(0.3f * d, -0.15f * d); b.vertex(0.75f * d, -0.75f * d);
    b.vertex(0.3f * d, -0.55f * d);
    b.end();
    b.begin(GL_LINES);       // exhaust
    b.vertex(0, -0.65f * d); b.vertex(0, -0.95f * d);
    b.end();
  });
  glow_mesh.upload(mb);
}

void MissilePickup::apply(Ship *ship) {
  ship->add_missile_ammo(10);
}

void MissilePickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
