#ifndef PICKUP_H
#define PICKUP_H

#include "object.h"
#include "mesh.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class Ship;

class Pickup : public Object {
public:
  Pickup(WrappedPoint pos) : collected(false) {
    position = pos;
    velocity = Point(0, 0);
    radius = 20.0f;
    rotation_speed = 0.0f;
    alive = true;
  }
  virtual ~Pickup() {}
  virtual void draw(float world_rotation = 0.0f) const = 0;
  virtual void apply(Ship *ship) = 0;
  bool is_removable() const override { return collected; }
  bool collected;

  Mesh glow_mesh;

protected:
  // Fill mb with a glowing 5-pointed star (4 layers: 3 halo + 1 solid).
  static void build_glow_star(MeshBuilder& mb, float r, float g, float b,
                               float outer, float inner) {
    struct Layer { float scale; float alpha; };
    static const Layer layers[] = {
      {2.0f,  0.05f},
      {1.5f,  0.12f},
      {1.15f, 0.28f},
      {1.0f,  1.0f },
    };
    for (const Layer& L : layers) {
      mb.begin(GL_LINE_LOOP);
      mb.color(r, g, b, L.alpha);
      for (int i = 0; i < 10; i++) {
        float angle = i * (float)M_PI / 5.0f - (float)M_PI / 2.0f;
        float rad = (i % 2 == 0) ? outer * L.scale : inner * L.scale;
        mb.vertex(cosf(angle) * rad, sinf(angle) * rad);
      }
      mb.end();
    }
  }

  // Fill mb with a glowing heart shape (4 layers: 3 halo + 1 solid).
  static void build_glow_heart(MeshBuilder& mb, float r, float g, float b,
                                float s) {
    struct Layer { float scale; float alpha; };
    static const Layer layers[] = {
      {2.0f,  0.05f},
      {1.5f,  0.12f},
      {1.15f, 0.28f},
      {1.0f,  1.0f },
    };
    for (const Layer& L : layers) {
      float sc = s * L.scale;
      mb.begin(GL_LINE_LOOP);
      mb.color(r, g, b, L.alpha);
      mb.vertex( 0.0f * sc,  0.5f * sc);
      mb.vertex( 0.5f * sc,  1.0f * sc);
      mb.vertex( 1.0f * sc,  0.5f * sc);
      mb.vertex( 0.5f * sc,  0.0f * sc);
      mb.vertex( 0.0f * sc, -1.0f * sc);
      mb.vertex(-0.5f * sc,  0.0f * sc);
      mb.vertex(-1.0f * sc,  0.5f * sc);
      mb.vertex(-0.5f * sc,  1.0f * sc);
      mb.end();
    }
  }
};

#endif
