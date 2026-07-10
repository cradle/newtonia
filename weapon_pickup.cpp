#include "weapon_pickup.h"
#include "ship.h"
#include "gl_compat.h"

WeaponPickup::WeaponPickup(WrappedPoint pos, int weapon_index) :
  Pickup(pos), weapon_index(weapon_index) {
  // A crosshair: circle with four ticks.
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_icon(mb, 0.0f, 1.0f, 0.0f, [s](MeshBuilder& b, float k) {
    float d = s * k;
    b.begin(GL_LINE_LOOP);
    for (int i = 0; i < 12; i++) {
      float a = i * 2.0f * (float)M_PI / 12.0f;
      b.vertex(cosf(a) * 0.55f * d, sinf(a) * 0.55f * d);
    }
    b.end();
    b.begin(GL_LINES);
    b.vertex(0.3f * d, 0);  b.vertex(0.95f * d, 0);
    b.vertex(-0.3f * d, 0); b.vertex(-0.95f * d, 0);
    b.vertex(0, 0.3f * d);  b.vertex(0, 0.95f * d);
    b.vertex(0, -0.3f * d); b.vertex(0, -0.95f * d);
    b.end();
  });
  glow_mesh.upload(mb);
}

void WeaponPickup::apply(Ship *ship) {
  ship->add_weapon(weapon_index);
}

void WeaponPickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
