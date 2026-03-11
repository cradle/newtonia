#include "weapon_pickup.h"
#include "gl_compat.h"

WeaponPickup::WeaponPickup(WrappedPoint pos, int weapon_index)
  : collected(false), weapon_index(weapon_index) {
  position = pos;
  velocity = Point(0, 0);
  radius = 20.0f;
  rotation_speed = 0.0f;
  alive = true;
}

bool WeaponPickup::is_removable() const {
  return collected;
}

void WeaponPickup::draw(float world_rotation) const {
  float s = radius * 0.8f;

  glTranslatef(position.x(), position.y(), 0.0f);
  // Counter-rotate so the icon always appears upright
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glColor3f(0.0f, 1.0f, 0.0f);

  // Diamond shape with a cross inside to suggest a weapon/powerup
  glBegin(GL_LINE_LOOP);
    glVertex2f( 0.0f * s,  1.0f * s);  // top
    glVertex2f( 0.7f * s,  0.0f * s);  // right
    glVertex2f( 0.0f * s, -1.0f * s);  // bottom
    glVertex2f(-0.7f * s,  0.0f * s);  // left
  glEnd();

  // Cross inside the diamond
  glBegin(GL_LINES);
    glVertex2f( 0.0f * s,  0.5f * s);
    glVertex2f( 0.0f * s, -0.5f * s);
    glVertex2f(-0.35f * s, 0.0f * s);
    glVertex2f( 0.35f * s, 0.0f * s);
  glEnd();
}
