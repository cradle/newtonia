#include "god_mode_pickup.h"
#include "ship.h"
#include "gl_compat.h"

GodModePickup::GodModePickup(WrappedPoint pos) : Pickup(pos) {
  display_list = glGenLists(1);
  glNewList(display_list, GL_COMPILE);
  draw_glow_lightning(1.0f, 0.9f, 0.0f, radius * 0.8f);
  glEndList();
}

GodModePickup::~GodModePickup() {
  glDeleteLists(display_list, 1);
}

void GodModePickup::apply(Ship *ship) {
  ship->add_god_mode();
}

void GodModePickup::draw(float world_rotation) const {
  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glCallList(display_list);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
