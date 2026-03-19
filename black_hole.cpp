#include "black_hole.h"
#include "gl_compat.h"
#include <math.h>

// Gravitational strength constant (G*M in game units).
// At 400 units this imparts ~0.01 units/ms per step — a gentle but accumulating pull.
const float BlackHole::gravitational_strength = 50.0f;

// Objects beyond this distance are not affected (performance optimisation).
const float BlackHole::influence_radius = 800.0f;

BlackHole::BlackHole(WrappedPoint pos) {
  position = pos;
  velocity = Point(0.0f, 0.0f);
  rotation_speed = 0.5f;  // slow visual spin
  radius = 40.0f;
  radius_squared = radius * radius;
  alive = true;
  invincible = true;  // cannot be shot/killed
  pulse_phase = 0.0f;
}

void BlackHole::step(int delta) {
  rotation += rotation_speed * delta;
  pulse_phase += 0.003f * delta;
  if (pulse_phase > 2.0f * (float)M_PI)
    pulse_phase -= 2.0f * (float)M_PI;
  // Black hole does not move.
}

bool BlackHole::is_removable() const {
  return false;  // permanent fixture
}

bool BlackHole::apply_gravity(Object &other, int delta) const {
  // Use closest wrapped copy of the black hole to handle world-edge wrap-around.
  Point bh_pos = position.closest_to(other.position);
  Point diff = bh_pos - other.position;
  float dist_sq = diff.magnitude_squared();

  if (dist_sq > influence_radius * influence_radius)
    return false;

  float dist = sqrtf(dist_sq);

  // Event horizon check: object centre inside black hole radius.
  if (dist < radius)
    return true;  // caller should kill this object

  // Gravitational acceleration: a = G*M / r^2, directed toward the black hole.
  // Clamp the effective distance to a minimum (softened gravity) so that objects
  // just outside the event horizon don't receive a near-infinite impulse and get
  // flung across the map at ridiculous speed.
  const float softening = radius * 2.0f;  // 80 units
  float soft_dist_sq = dist_sq < softening * softening ? softening * softening : dist_sq;
  float accel = gravitational_strength / soft_dist_sq * (float)delta;
  Point force = diff.normalized() * accel;
  other.velocity += force;

  return false;
}

void BlackHole::draw(bool is_minimap) const {
  const int segments = 32;
  float pulse = 0.15f * sinf(pulse_phase);

  glPushMatrix();
  glTranslatef(position.x(), position.y(), 0.0f);

  if (is_minimap) {
    // Simple filled black dot with a white ring on the minimap.
    glColor3f(0.0f, 0.0f, 0.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(0.0f, 0.0f);
    for (int i = 0; i <= segments; i++) {
      float a = i * 2.0f * (float)M_PI / segments;
      glVertex2f(cosf(a) * radius, sinf(a) * radius);
    }
    glEnd();
    glColor3f(0.6f, 0.3f, 1.0f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++) {
      float a = i * 2.0f * (float)M_PI / segments;
      glVertex2f(cosf(a) * radius, sinf(a) * radius);
    }
    glEnd();
    glPopMatrix();
    return;
  }

  // --- Filled black circle (drawn first so accretion disk renders on top) ---
  glDisable(GL_BLEND);
  glColor3f(0.0f, 0.0f, 0.0f);
  glBegin(GL_TRIANGLE_FAN);
  glVertex2f(0.0f, 0.0f);
  for (int i = 0; i <= segments; i++) {
    float a = i * 2.0f * (float)M_PI / segments;
    glVertex2f(cosf(a) * radius * 4.0f, sinf(a) * radius * 4.0f);
  }
  glEnd();
  glEnable(GL_BLEND);

  // --- Accretion disk (glowing outer ring) ---
  float disk_inner = radius * 1.15f;
  float disk_outer = radius * (2.2f + pulse);
  glLineWidth(1.0f);
  for (int ring = 0; ring < 3; ring++) {
    float t = ring / 2.0f;
    float r_ring = disk_inner + (disk_outer - disk_inner) * t;
    float alpha  = (1.0f - t) * 0.7f;
    glColor4f(0.8f - t * 0.4f, 0.2f + t * 0.1f, 1.0f - t * 0.3f, alpha);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++) {
      float a = i * 2.0f * (float)M_PI / segments + rotation * (float)M_PI / 180.0f;
      float wobble = 1.0f + 0.06f * sinf(3.0f * a + pulse_phase);
      glVertex2f(cosf(a) * r_ring * wobble, sinf(a) * r_ring * wobble * 0.5f);
    }
    glEnd();
  }

  glPopMatrix();
}
