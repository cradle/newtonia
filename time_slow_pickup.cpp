#include "time_slow_pickup.h"
#include "ship.h"
#include "gl_compat.h"

TimeSlowPickup::TimeSlowPickup(WrappedPoint pos) : Pickup(pos) {
  // A silver-white clock face: 16-segment dial, quarter-hour ticks, and
  // hands reading ten-past-ten (the classic storefront pose).
  float s = radius * 0.9f;
  MeshBuilder mb;
  build_glow_icon(mb, 0.95f, 0.95f, 1.0f, [s](MeshBuilder& b, float k) {
    float d = s * k;
    b.begin(GL_LINE_LOOP);
    for (int i = 0; i < 16; i++) {
      float a = i * 2.0f * (float)M_PI / 16.0f;
      b.vertex(cosf(a) * d, sinf(a) * d);
    }
    b.end();
    b.begin(GL_LINES);
    for (int i = 0; i < 4; i++) {
      float a = i * (float)M_PI / 2.0f;
      b.vertex(cosf(a) * 0.82f * d, sinf(a) * 0.82f * d);
      b.vertex(cosf(a) * d, sinf(a) * d);
    }
    b.vertex(0.0f, 0.0f); b.vertex(-0.34f * d, 0.52f * d);  // hour hand
    b.vertex(0.0f, 0.0f); b.vertex( 0.40f * d, 0.60f * d);  // minute hand
    b.end();
  });
  glow_mesh.upload(mb);
}

// The effect belongs to the world clock, not the collector's ship — GLGame
// starts it at the collection site (same pattern as the revive).
void TimeSlowPickup::apply(Ship *ship) {
  (void)ship;
}

void TimeSlowPickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  // Counter-rotate so the clock always appears upright.
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
