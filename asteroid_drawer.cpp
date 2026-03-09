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
    Typer::draw(0.0f, 0.0f, object->value, 9);
    glPopMatrix();
  }
}

// draw_batch renders all alive asteroids in two draw calls (fill + outline),
// then handles dead asteroid debris/scores individually. Vertices are computed
// in world space so the caller only needs a tile-offset matrix transform.
void AsteroidDrawer::draw_batch(list<Asteroid*> const *objects, float direction, bool is_minimap) {
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

    float vx[9], vy[9];
    for (int i = 0; i < segs; i++) {
      float angle = rot + i * step;
      vx[i] = cx + r * cosf(angle);
      vy[i] = cy + r * sinf(angle);
    }
    // Triangle fan decomposition (v0, vi, vi+1)
    for (int i = 1; i < segs - 1; i++) {
      glVertex2f(vx[0],   vy[0]);
      glVertex2f(vx[i],   vy[i]);
      glVertex2f(vx[i+1], vy[i+1]);
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

    float vx[9], vy[9];
    for (int i = 0; i < segs; i++) {
      float angle = rot + i * step;
      vx[i] = cx + r * cosf(angle);
      vy[i] = cy + r * sinf(angle);
    }
    // LINE_LOOP as explicit pairs
    for (int i = 0; i < segs; i++) {
      int j = (i + 1) % segs;
      glVertex2f(vx[i], vy[i]);
      glVertex2f(vx[j], vy[j]);
    }
  }
  glEnd();

  // --- Dead asteroids: debris particles (batched) + score text ---
  if (!is_minimap) {
    // All debris from all dead asteroids in one GL_POINTS call.
    glPointSize(3.0f);
    glBegin(GL_POINTS);
    for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
      Asteroid const *a = *it;
      if (a->alive) continue;
      for (list<Particle>::const_iterator d = a->debris.begin(); d != a->debris.end(); ++d) {
        float alpha = rand() / (float)RAND_MAX * d->aliveness() / 2.0f + d->aliveness() / 2.0f;
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
      Typer::draw(0.0f, 0.0f, a->value, 9);
      glPopMatrix();
    }
  }
}

void AsteroidDrawer::draw_debris(list<Particle> const &debris) {
  glPointSize(3.0f);
  glBegin(GL_POINTS);
  for(list<Particle>::const_iterator d = debris.begin(); d != debris.end(); d++) {
    glColor4f(1.0f, 1.0f, 1.0f, rand()/(1.0f*(float)RAND_MAX) * d->aliveness()/2.0f + d->aliveness()/2.0f);
		glVertex2fv(d->position);
  }
	glEnd();
}
