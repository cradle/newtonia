#include "nova_charge_pickup.h"
#include "ship.h"
#include "gl_compat.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

NovaChargePickup::NovaChargePickup(WrappedPoint pos) : Pickup(pos) {
  // Glowing ring shape — a mini nova shockwave in orange-gold
  MeshBuilder mb;
  const int segs = 16;
  struct Layer { float scale; float alpha; };
  static const Layer layers[] = {
    {2.0f,  0.05f},
    {1.5f,  0.12f},
    {1.15f, 0.28f},
    {1.0f,  1.0f },
  };
  float r = radius * 0.7f;
  for (const Layer& L : layers) {
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 0.6f, 0.1f, L.alpha);
    for (int i = 0; i < segs; i++) {
      float a = i * 2.0f * (float)M_PI / segs;
      mb.vertex(cosf(a) * r * L.scale, sinf(a) * r * L.scale);
    }
    mb.end();
  }
  glow_mesh.upload(mb);
}

void NovaChargePickup::apply(Ship *ship) {
  ship->add_nova_charge(1);
}

void NovaChargePickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
