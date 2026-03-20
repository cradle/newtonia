#include "asteroid_drawer.h"
#include "asteroid.h"
#include "typer.h"
#include <math.h>

#include "gl_compat.h"

#include <iostream>

const int AsteroidDrawer::number_of_segments = 7;

int AsteroidDrawer::seg_count(float radius) {
  int s = number_of_segments;
  if      (radius < 15)  s -= 2;
  else if (radius < 30)  s -= 1;
  else if (radius > 200) s += 2;
  return s;
}

// draw() is kept for dead asteroids (score + debris) called from legacy paths.
void AsteroidDrawer::draw(Asteroid const *object, float direction, bool is_minimap) {
  if(object->alive && !object->invisible) {
    glPushMatrix();
    glTranslatef(object->position.x(), object->position.y(), 0.0f);
    glScalef(object->radius, object->radius, 1.0f);
    glRotatef(object->rotation, 0.0f, 0.0f, 1.0f);
    if(object->invincible) {
      glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
    } else {
      glColor3f(0.0f, 0.0f, 0.0f);
    }
    if(is_minimap) {
      glLineWidth(1.0f);
    } else {
      glLineWidth(2.5f);
    }
    int sc = seg_count(object->radius);
    float segment_size = 360.0f / sc, d;
    glBegin(GL_POLYGON);
    for (int vi = 0; vi < sc; vi++) {
      d = vi * segment_size * (float)M_PI / 180.0f;
      float off = object->vertex_offsets[vi];
      glVertex2f(off * cosf(d), off * sinf(d));
    }
    glEnd();
    if(object->invincible) {
      glColor4f(0.8f, 0.8f, 0.8f, 0.8f);
    } else {
      glColor3f(1.0f, 1.0f, 1.0f);
    }
    glBegin(GL_LINE_LOOP);
    for (int vi = 0; vi < sc; vi++) {
      d = vi * segment_size * (float)M_PI / 180.0f;
      float off = object->vertex_offsets[vi];
      glVertex2f(off * cosf(d), off * sinf(d));
    }
    glEnd();
    glPopMatrix();
  } else if(!is_minimap) {
    draw_debris(object->debris);
    glPushMatrix();
    glTranslatef(object->position.x(), object->position.y(), 0.0f);
    glRotatef(-direction, 0.0f, 0.0f, 1.0f);
    Typer::draw(0.0f, 0.0f, object->value, 18.0f / Typer::scale);
    glPopMatrix();
  }
}

void AsteroidDrawer::draw_invisible_mask(Asteroid const *object, float x, float y) {
  int segs = seg_count(object->radius);
  float step = 2.0f * (float)M_PI / segs;
  float rot = object->rotation * (float)M_PI / 180.0f;
  glColor3f(0.0f, 0.0f, 0.0f);
  glBegin(GL_POLYGON);
  for (int i = 0; i < segs; i++) {
    float angle = rot + i * step;
    float off = object->vertex_offsets[i];
    glVertex2f(x + object->radius * off * cosf(angle), y + object->radius * off * sinf(angle));
  }
  glEnd();
  glLineWidth(2.5f);
  glBegin(GL_LINE_LOOP);
  for (int i = 0; i < segs; i++) {
    float angle = rot + i * step;
    float off = object->vertex_offsets[i];
    glVertex2f(x + object->radius * off * cosf(angle), y + object->radius * off * sinf(angle));
  }
  glEnd();
}

// Cached per-asteroid vertex data, computed once and shared between fill and
// outline passes to avoid redundant cosf/sinf calls.
struct AsteroidVerts {
  float dvx[9], dvy[9];
  float cx, cy, dx, dy;
  int segs;
  bool invincible;
  bool invisible;
};

// draw_batch renders all alive asteroids in two draw calls (fill + outline),
// then handles dead asteroid debris/scores individually. Vertices are computed
// once per asteroid and reused for both passes.
// When wrap_x/wrap_y are non-zero (minimap), asteroids near world edges are
// drawn again at the opposite edge so wrapping is visible.
void AsteroidDrawer::draw_batch(list<Asteroid*> const *objects, list<Asteroid*> const *dead_objects,
                                float direction, bool is_minimap,
                                float wrap_x, float wrap_y) {
  // --- Pre-compute vertex data for all alive asteroids (trig once per asteroid) ---
  vector<AsteroidVerts> verts;
  verts.reserve(objects->size());
  for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
    Asteroid const *a = *it;
    AsteroidVerts v;
    v.cx = a->position.x();
    v.cy = a->position.y();
    float r   = a->radius;
    float rot = a->rotation * (float)M_PI / 180.0f;
    v.segs    = seg_count(r);
    float step = 2.0f * (float)M_PI / v.segs;
    for (int i = 0; i < v.segs; i++) {
      float angle = rot + i * step;
      float off = a->vertex_offsets[i];
      v.dvx[i] = r * off * cosf(angle);
      v.dvy[i] = r * off * sinf(angle);
    }
    v.dx = 0; v.dy = 0;
    if (wrap_x > 0) {
      if (v.cx < r)               v.dx = wrap_x;
      else if (v.cx + r > wrap_x) v.dx = -wrap_x;
      if (v.cy < r)               v.dy = wrap_y;
      else if (v.cy + r > wrap_y) v.dy = -wrap_y;
    }
    v.invincible = a->invincible;
    v.invisible  = a->invisible;
    verts.push_back(v);
  }

  // --- Fill pass: all alive asteroids as GL_TRIANGLES ---
  glBegin(GL_TRIANGLES);
  for (size_t ai = 0; ai < verts.size(); ++ai) {
    AsteroidVerts const &v = verts[ai];
    if (v.invisible) continue;
    if (v.invincible) glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
    else              glColor3f(0.0f, 0.0f, 0.0f);
    for (int wi = 0; wi < (v.dx != 0 ? 2 : 1); wi++) {
      for (int wj = 0; wj < (v.dy != 0 ? 2 : 1); wj++) {
        float wcx = v.cx + wi * v.dx;
        float wcy = v.cy + wj * v.dy;
        for (int i = 0; i < v.segs; i++) {
          int j = (i + 1) % v.segs;
          glVertex2f(wcx,               wcy);
          glVertex2f(wcx + v.dvx[i],   wcy + v.dvy[i]);
          glVertex2f(wcx + v.dvx[j],   wcy + v.dvy[j]);
        }
      }
    }
  }
  glEnd();

  // --- Outline pass: all alive asteroids as GL_LINES ---
  glLineWidth(is_minimap ? 1.0f : 2.5f);
  glBegin(GL_LINES);
  for (size_t ai = 0; ai < verts.size(); ++ai) {
    AsteroidVerts const &v = verts[ai];
    if (v.invisible) continue;
    if (v.invincible) glColor4f(0.8f, 0.8f, 0.8f, 0.8f);
    else              glColor3f(1.0f, 1.0f, 1.0f);
    for (int wi = 0; wi < (v.dx != 0 ? 2 : 1); wi++) {
      for (int wj = 0; wj < (v.dy != 0 ? 2 : 1); wj++) {
        float wcx = v.cx + wi * v.dx;
        float wcy = v.cy + wj * v.dy;
        for (int i = 0; i < v.segs; i++) {
          int j = (i + 1) % v.segs;
          glVertex2f(wcx + v.dvx[i], wcy + v.dvy[i]);
          glVertex2f(wcx + v.dvx[j], wcy + v.dvy[j]);
        }
      }
    }
  }
  glEnd();

  // --- Dead asteroids: debris particles (batched) + score text ---
  if (!is_minimap) {
    static float flicker[64];
    static bool flicker_init = false;
    static int flicker_idx = 0;
    if (!flicker_init) {
      for (int i = 0; i < 64; i++)
        flicker[i] = rand() / (float)RAND_MAX;
      flicker_init = true;
    }

    // All debris from all dead asteroids in one GL_POINTS call.
    glPointSize(3.0f);
    glBegin(GL_POINTS);
    for (list<Asteroid*>::const_iterator it = dead_objects->begin(); it != dead_objects->end(); ++it) {
      Asteroid const *a = *it;
      for (auto d = a->debris.begin(); d != a->debris.end(); ++d) {
        float alive = d->aliveness();
        float alpha = flicker[flicker_idx++ % 64] * alive / 2.0f + alive / 2.0f;
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glVertex2fv(d->position);
      }
    }
    glEnd();
    // Score text must be drawn per-asteroid (Typer is not batchable).
    for (list<Asteroid*>::const_iterator it = dead_objects->begin(); it != dead_objects->end(); ++it) {
      Asteroid const *a = *it;
      glPushMatrix();
      glTranslatef(a->position.x(), a->position.y(), 0.0f);
      glRotatef(-direction, 0.0f, 0.0f, 1.0f);
      Typer::draw(0.0f, 0.0f, a->value, 18.0f / Typer::scale);
      glPopMatrix();
    }
  }
}

void AsteroidDrawer::draw_debris(vector<Particle> const &debris) {
  static float flicker[64];
  static bool flicker_init = false;
  static int flicker_idx = 0;
  if (!flicker_init) {
    for (int i = 0; i < 64; i++)
      flicker[i] = rand() / (float)RAND_MAX;
    flicker_init = true;
  }
  glPointSize(3.0f);
  glBegin(GL_POINTS);
  for(auto d = debris.begin(); d != debris.end(); d++) {
    float alive = d->aliveness();
    glColor4f(1.0f, 1.0f, 1.0f, flicker[flicker_idx++ % 64] * alive / 2.0f + alive / 2.0f);
		glVertex2fv(d->position);
  }
	glEnd();
}
