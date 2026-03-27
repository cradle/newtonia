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

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
  }

  // Draw a glowing lightning bolt. Call after glTranslatef/glRotatef.
  static void draw_glow_lightning(float r, float g, float b, float s) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    // 8-vertex ⚡ polygon: upper-right arm, middle notch, lower-left arm
    static const float pts[][2] = {
      {  0.2f,  1.0f },   // top-left of upper arm
      {  0.6f,  1.0f },   // top-right
      {  0.1f,  0.1f },   // inner-right of middle
      {  0.5f,  0.1f },   // outer-right of notch
      { -0.2f, -1.0f },   // bottom-right of lower arm
      { -0.6f, -1.0f },   // bottom-left
      { -0.1f, -0.1f },   // inner-left of middle
      { -0.5f, -0.1f },   // outer-left of notch
    };

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
      for (int i = 0; i < 8; i++)
        glVertex2f(pts[i][0] * s * L.scale, pts[i][1] * s * L.scale);
      glEnd();
    }

    glLineWidth(1.8f);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 8; i++)
      glVertex2f(pts[i][0] * s, pts[i][1] * s);
    glEnd();

    glLineWidth(1.0f);
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

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
  }
};

#endif
