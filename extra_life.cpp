#include "extra_life.h"
#include "gl_compat.h"

ExtraLife::ExtraLife(WrappedPoint pos) : collected(false) {
  position = pos;
  velocity = Point(0, 0);
  radius = 20.0f;
  rotation_speed = 0.5f;
  alive = true;
}

bool ExtraLife::is_removable() const {
  return collected;
}

void ExtraLife::draw() const {
  float s = radius * 0.8f;

  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(rotation, 0.0f, 0.0f, 1.0f);
  glColor3f(1.0f, 0.0f, 0.0f);

  // Angular heart: 8 vertices forming two bumps at top and a V-point at bottom
  glBegin(GL_LINE_LOOP);
    glVertex2f(    0.0f * s,  0.5f * s);  // center top dip
    glVertex2f(    0.5f * s,  1.0f * s);  // right bump top
    glVertex2f(    1.0f * s,  0.5f * s);  // right side
    glVertex2f(    0.5f * s,  0.0f * s);  // right base
    glVertex2f(    0.0f * s, -1.0f * s);  // bottom point
    glVertex2f(   -0.5f * s,  0.0f * s);  // left base
    glVertex2f(   -1.0f * s,  0.5f * s);  // left side
    glVertex2f(   -0.5f * s,  1.0f * s);  // left bump top
  glEnd();
}
