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
  if(object->alive) {
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
    for (float i = 0.0f; i < 360.0f; i += segment_size) {
      d = i * (float)M_PI / 180.0f;
      glVertex2f(cosf(d), sinf(d));
    }
    glEnd();
    if(object->invincible) {
      glColor4f(0.8f, 0.8f, 0.8f, 0.8f);
    } else {
      glColor3f(1.0f, 1.0f, 1.0f);
    }
    glBegin(GL_LINE_LOOP);
    for (float i = 0.0f; i < 360.0f; i += segment_size) {
      d = i * (float)M_PI / 180.0f;
      glVertex2f(cosf(d), sinf(d));
    }
    glEnd();
    glPopMatrix();
  } else if(!is_minimap) {
    draw_debris(object->debris);
    glPushMatrix();
    glTranslatef(object->position.x(), object->position.y(), 0.0f);
    glRotatef(-direction, 0.0f, 0.0f, 1.0f);
    Typer::draw(0.0f, 0.0f, object->value, 9.0f / Typer::scale);
    glPopMatrix();
  }
}

// draw_batch renders all alive asteroids in two draw calls (fill + outline),
// then handles dead asteroid debris/scores individually. Vertices are computed
// in world space so the caller only needs a tile-offset matrix transform.
// When wrap_x/wrap_y are non-zero (minimap), asteroids near world edges are
// drawn again at the opposite edge so wrapping is visible.
void AsteroidDrawer::draw_batch(list<Asteroid*> const *objects, float direction, bool is_minimap,
                                float wrap_x, float wrap_y) {
  // --- Fill pass: all alive asteroids as GL_TRIANGLES ---
  glBegin(GL_TRIANGLES);
  for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
    Asteroid const *a = *it;
    if (!a->alive) continue;
    float cx = a->position.x(), cy = a->position.y();
    float r  = a->radius;
    float rot = a->rotation * (float)M_PI / 180.0f;
    int segs  = seg_count(r);
    float step = 2.0f * (float)M_PI / segs;

    if (a->invincible) glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
    else               glColor3f(0.0f, 0.0f, 0.0f);

    // Compute vertices relative to center (trig only once per asteroid)
    float dvx[9], dvy[9];
    for (int i = 0; i < segs; i++) {
      float angle = rot + i * step;
      dvx[i] = r * cosf(angle);
      dvy[i] = r * sinf(angle);
    }

    // Determine wrap offsets for edge-adjacent asteroids
    float dx = 0, dy = 0;
    if (wrap_x > 0) {
      if (cx < r)              dx = wrap_x;
      else if (cx + r > wrap_x) dx = -wrap_x;
      if (cy < r)              dy = wrap_y;
      else if (cy + r > wrap_y) dy = -wrap_y;
    }

    // Emit vertices at original position + any wrap copies
    for (int wi = 0; wi < (dx != 0 ? 2 : 1); wi++) {
      for (int wj = 0; wj < (dy != 0 ? 2 : 1); wj++) {
        float wcx = cx + wi * dx;
        float wcy = cy + wj * dy;
        for (int i = 1; i < segs - 1; i++) {
          glVertex2f(wcx + dvx[0],   wcy + dvy[0]);
          glVertex2f(wcx + dvx[i],   wcy + dvy[i]);
          glVertex2f(wcx + dvx[i+1], wcy + dvy[i+1]);
        }
      }
    }
  }
  glEnd();

  // --- Outline pass: all alive asteroids as GL_LINES ---
  glLineWidth(is_minimap ? 1.0f : 2.5f);
  glBegin(GL_LINES);
  for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
    Asteroid const *a = *it;
    if (!a->alive) continue;
    float cx = a->position.x(), cy = a->position.y();
    float r   = a->radius;
    float rot = a->rotation * (float)M_PI / 180.0f;
    int segs  = seg_count(r);
    float step = 2.0f * (float)M_PI / segs;

    if (a->invincible) glColor4f(0.8f, 0.8f, 0.8f, 0.8f);
    else               glColor3f(1.0f, 1.0f, 1.0f);

    float dvx[9], dvy[9];
    for (int i = 0; i < segs; i++) {
      float angle = rot + i * step;
      dvx[i] = r * cosf(angle);
      dvy[i] = r * sinf(angle);
    }

    float dx = 0, dy = 0;
    if (wrap_x > 0) {
      if (cx < r)              dx = wrap_x;
      else if (cx + r > wrap_x) dx = -wrap_x;
      if (cy < r)              dy = wrap_y;
      else if (cy + r > wrap_y) dy = -wrap_y;
    }

    for (int wi = 0; wi < (dx != 0 ? 2 : 1); wi++) {
      for (int wj = 0; wj < (dy != 0 ? 2 : 1); wj++) {
        float wcx = cx + wi * dx;
        float wcy = cy + wj * dy;
        for (int i = 0; i < segs; i++) {
          int j = (i + 1) % segs;
          glVertex2f(wcx + dvx[i], wcy + dvy[i]);
          glVertex2f(wcx + dvx[j], wcy + dvy[j]);
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
    for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
      Asteroid const *a = *it;
      if (a->alive) continue;
      for (auto d = a->debris.begin(); d != a->debris.end(); ++d) {
        float alive = d->aliveness();
        float alpha = flicker[flicker_idx++ % 64] * alive / 2.0f + alive / 2.0f;
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glVertex2fv(d->position);
      }
    }
    glEnd();
    // Score text must be drawn per-asteroid (Typer is not batchable).
    for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
      Asteroid const *a = *it;
      if (a->alive) continue;
      glPushMatrix();
      glTranslatef(a->position.x(), a->position.y(), 0.0f);
      glRotatef(-direction, 0.0f, 0.0f, 1.0f);
      Typer::draw(0.0f, 0.0f, a->value, 9.0f / Typer::scale);
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
