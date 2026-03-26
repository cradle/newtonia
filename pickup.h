#ifndef PICKUP_H
#define PICKUP_H

#include "object.h"
#include "gl_compat.h"
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

protected:
  // Draw a glowing 5-pointed star. Call after glTranslatef/glRotatef.
  // Renders additive-blended halo layers then the solid core line.
  static void draw_glow_star(float r, float g, float b, float outer, float inner) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    struct Layer { float scale; float alpha; float lw; };
    static const Layer layers[] = {
      {2.0f,  0.05f, 6.0f},
      {1.5f,  0.12f, 4.0f},
      {1.15f, 0.28f, 2.5f},
    };
    for (const Layer& L : layers) {
      glLineWidth(L.lw);
      glColor4f(r, g, b, L.alpha);
      glBegin(GL_LINE_LOOP);
      for (int i = 0; i < 10; i++) {
        float angle = i * (float)M_PI / 5.0f - (float)M_PI / 2.0f;
        float rad = (i % 2 == 0) ? outer * L.scale : inner * L.scale;
        glVertex2f(cosf(angle) * rad, sinf(angle) * rad);
      }
      glEnd();
    }

    glLineWidth(1.8f);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 10; i++) {
      float angle = i * (float)M_PI / 5.0f - (float)M_PI / 2.0f;
      float rad = (i % 2 == 0) ? outer : inner;
      glVertex2f(cosf(angle) * rad, sinf(angle) * rad);
    }
    glEnd();

    glDisable(GL_BLEND);
  }

  // Draw a glowing heart shape. Call after glTranslatef/glRotatef.
  static void draw_glow_heart(float r, float g, float b, float s) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    struct Layer { float scale; float alpha; float lw; };
    static const Layer layers[] = {
      {2.0f,  0.05f, 6.0f},
      {1.5f,  0.12f, 4.0f},
      {1.15f, 0.28f, 2.5f},
    };
    for (const Layer& L : layers) {
      float sc = s * L.scale;
      glLineWidth(L.lw);
      glColor4f(r, g, b, L.alpha);
      glBegin(GL_LINE_LOOP);
        glVertex2f( 0.0f * sc,  0.5f * sc);
        glVertex2f( 0.5f * sc,  1.0f * sc);
        glVertex2f( 1.0f * sc,  0.5f * sc);
        glVertex2f( 0.5f * sc,  0.0f * sc);
        glVertex2f( 0.0f * sc, -1.0f * sc);
        glVertex2f(-0.5f * sc,  0.0f * sc);
        glVertex2f(-1.0f * sc,  0.5f * sc);
        glVertex2f(-0.5f * sc,  1.0f * sc);
      glEnd();
    }

    glLineWidth(1.8f);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_LINE_LOOP);
      glVertex2f( 0.0f * s,  0.5f * s);
      glVertex2f( 0.5f * s,  1.0f * s);
      glVertex2f( 1.0f * s,  0.5f * s);
      glVertex2f( 0.5f * s,  0.0f * s);
      glVertex2f( 0.0f * s, -1.0f * s);
      glVertex2f(-0.5f * s,  0.0f * s);
      glVertex2f(-1.0f * s,  0.5f * s);
      glVertex2f(-0.5f * s,  1.0f * s);
    glEnd();

    glDisable(GL_BLEND);
  }

  // Draw a glowing lightning bolt shape. Call after glTranslatef/glRotatef.
  static void draw_glow_bolt(float r, float g, float b, float s) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    struct Layer { float scale; float alpha; float lw; };
    static const Layer layers[] = {
      {2.0f,  0.05f, 6.0f},
      {1.5f,  0.12f, 4.0f},
      {1.15f, 0.28f, 2.5f},
    };
    for (const Layer& L : layers) {
      float sc = s * L.scale;
      glLineWidth(L.lw);
      glColor4f(r, g, b, L.alpha);
      glBegin(GL_LINE_LOOP);
        glVertex2f( 0.2f * sc,  1.0f * sc);
        glVertex2f(-0.2f * sc,  0.1f * sc);
        glVertex2f( 0.3f * sc,  0.1f * sc);
        glVertex2f(-0.2f * sc, -1.0f * sc);
        glVertex2f( 0.2f * sc, -0.1f * sc);
        glVertex2f(-0.3f * sc, -0.1f * sc);
      glEnd();
    }

    glLineWidth(2.0f);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_LINE_LOOP);
      glVertex2f( 0.2f * s,  1.0f * s);
      glVertex2f(-0.2f * s,  0.1f * s);
      glVertex2f( 0.3f * s,  0.1f * s);
      glVertex2f(-0.2f * s, -1.0f * s);
      glVertex2f( 0.2f * s, -0.1f * s);
      glVertex2f(-0.3f * s, -0.1f * s);
    glEnd();

    glDisable(GL_BLEND);
  }
};

#endif
