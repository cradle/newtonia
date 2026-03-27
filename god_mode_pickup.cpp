#include "god_mode_pickup.h"
#include "ship.h"
#include "gl_compat.h"

GodModePickup::GodModePickup(WrappedPoint pos) : Pickup(pos) {
}

void GodModePickup::apply(Ship *ship) {
  ship->add_god_mode();
}

void GodModePickup::draw(float world_rotation) const {
  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  draw_glow_lightning(1.0f, 0.9f, 0.0f, radius * 0.8f);
}
