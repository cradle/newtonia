#include "weapon_pickup.h"
#include "ship.h"
#include "gl_compat.h"

WeaponPickup::WeaponPickup(WrappedPoint pos, int weapon_index) :
  Pickup(pos), weapon_index(weapon_index) {
  float s = radius * 0.8f;
  MeshBuilder mb;
  build_glow_star(mb, 0.0f, 1.0f, 0.0f, s, s * 0.4f);
  glow_mesh.upload(mb);
}

void WeaponPickup::apply(Ship *ship) {
  ship->add_weapon(weapon_index);
}

void WeaponPickup::draw(float world_rotation) const {
  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw();
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
