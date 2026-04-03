#include "god_mode_pickup.h"
#include "ship.h"
#include "gl_compat.h"
#include "mesh.h"
#include <math.h>

GodModePickup::GodModePickup(WrappedPoint pos) : Pickup(pos) {
  float r = 1.0f, g = 0.9f, b = 0.0f;
  float s = radius * 0.8f;

  static const float pts[][2] = {
    {  0.2f,  1.0f }, {  0.6f,  1.0f }, {  0.1f,  0.1f }, {  0.5f,  0.1f },
    { -0.2f, -1.0f }, { -0.6f, -1.0f }, { -0.1f, -0.1f }, { -0.5f, -0.1f },
  };

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
    for (int i = 0; i < 8; i++)
      mb.vertex(pts[i][0] * s * L.scale, pts[i][1] * s * L.scale);
    mb.end();
  }
  glow_mesh.upload(mb);
}

GodModePickup::~GodModePickup() {
}

void GodModePickup::apply(Ship *ship) {
  ship->add_god_mode();
}

void GodModePickup::draw(float world_rotation) const {
  glTranslatef(position.x(), position.y(), 0.0f);
  glRotatef(-world_rotation, 0.0f, 0.0f, 1.0f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glow_mesh.draw();
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
