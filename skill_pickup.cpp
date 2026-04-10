#include "skill_pickup.h"
#include "ship.h"
#include "gl_compat.h"
#include "mesh.h"
#include <math.h>

SkillPickup::SkillPickup(WrappedPoint pos) : Pickup(pos) {
  // Cyan 6-pointed star — visually distinct from the 5-pointed weapon pickup
  float r = 0.0f, g = 0.85f, b = 1.0f;
  float outer = radius * 0.85f;
  float inner = outer * 0.42f;

  struct Layer { float scale; float alpha; };
  static const Layer layers[] = {
    {2.0f,  0.05f},
    {1.5f,  0.12f},
    {1.15f, 0.28f},
    {1.0f,  1.0f },
  };

  MeshBuilder mb;
  for (const Layer& L : layers) {
    mb.begin(GL_LINE_LOOP);
    mb.color(r, g, b, L.alpha);
    for (int i = 0; i < 12; i++) {
      float angle = i * (float)M_PI / 6.0f - (float)M_PI / 2.0f;
      float rad = (i % 2 == 0) ? outer * L.scale : inner * L.scale;
      mb.vertex(cosf(angle) * rad, sinf(angle) * rad);
    }
    mb.end();
  }
  glow_mesh.upload(mb);
}

void SkillPickup::apply(Ship *ship) {
  ship->add_skill_fragment(1);
}

void SkillPickup::draw(float world_rotation) const {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw_at(position.x(), position.y(), -world_rotation);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
