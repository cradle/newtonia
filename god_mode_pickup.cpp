#include "god_mode_pickup.h"
#include "asteroid.h"
#include "ship.h"

GodModePickup::GodModePickup(WrappedPoint pos) : Pickup(pos) {
}

void GodModePickup::apply(Ship * /*ship*/) {
  Asteroid::god_mode = true;
  Asteroid::god_mode_time_left = duration_ms;
}

void GodModePickup::draw(float world_rotation) const {
  float s = radius * 0.7f;
  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  draw_glow_bolt(1.0f, 0.9f, 0.0f, s); // gold
}
