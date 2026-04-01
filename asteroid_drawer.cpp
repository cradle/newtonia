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
    if(object->reflective) {
      glColor4f(0.0f, 0.4f, 0.5f, 0.6f);
    } else if(object->invincible) {
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
    if(object->reflective) {
      glColor4f(0.3f, 0.9f, 1.0f, 0.9f);
    } else if(object->invincible) {
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
}

// Cached per-asteroid vertex data, computed once and shared between fill and
// outline passes to avoid redundant cosf/sinf calls.
struct AsteroidVerts {
  float dvx[9], dvy[9];
  float cx, cy, dx, dy;
  int segs;
  float radius;
  bool invincible;
  bool invisible;
  bool reflective;
  bool teleporting;
  bool teleport_vulnerable;
  float teleport_angle;
  bool quantum;
  bool quantum_observed;
  bool tough;
  int health;
  int crack_vertex[5];
  float crack_t[5];
  float crack_perp[5];
  bool armoured;
  float armour_angle;
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
    v.radius = r;
    v.dx = 0; v.dy = 0;
    if (wrap_x > 0) {
      if (v.cx < r)               v.dx = wrap_x;
      else if (v.cx + r > wrap_x) v.dx = -wrap_x;
      if (v.cy < r)               v.dy = wrap_y;
      else if (v.cy + r > wrap_y) v.dy = -wrap_y;
    }
    v.invincible          = a->invincible;
    v.invisible           = a->invisible;
    v.reflective          = a->reflective;
    v.teleporting         = a->teleporting;
    v.teleport_vulnerable = a->teleport_vulnerable;
    v.teleport_angle      = a->teleport_angle;
    v.quantum             = a->quantum;
    v.quantum_observed    = a->quantum_observed;
    v.tough               = a->tough;
    v.health              = a->health;
    v.armoured            = a->armoured;
    v.armour_angle        = a->armour_angle;
    for(int k = 0; k < 5; k++) {
      v.crack_vertex[k] = a->crack_vertex[k];
      v.crack_t[k]      = a->crack_t[k];
      v.crack_perp[k]   = a->crack_perp[k];
    }
    verts.push_back(v);
  }

  // --- Fill pass: invisible asteroids first (behind), then visible ---
  glBegin(GL_TRIANGLES);
  for (size_t ai = 0; ai < verts.size(); ++ai) {
    AsteroidVerts const &v = verts[ai];
    if (!v.invisible) continue;
    glColor3f(0.0f, 0.0f, 0.0f);
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
  for (size_t ai = 0; ai < verts.size(); ++ai) {
    AsteroidVerts const &v = verts[ai];
    if (v.invisible) continue;
    if (v.quantum && v.quantum_observed)        glColor4f(0.15f, 0.0f, 0.35f, 0.85f);
    else if (v.quantum)                         glColor4f(0.1f, 0.0f, 0.25f, 0.6f);
    else if (v.teleporting)                     glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    else if (v.reflective)                      glColor4f(0.0f, 0.4f, 0.5f, 0.6f);
    else if (v.invincible)                      glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
    else if (v.armoured)                        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    else                                        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
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

  // --- Outline pass: visible asteroids only (invisible ones are solid black, no outline needed) ---
  glLineWidth(is_minimap ? 1.0f : 2.5f);
  glBegin(GL_LINES);
  for (size_t ai = 0; ai < verts.size(); ++ai) {
    AsteroidVerts const &v = verts[ai];
    if (v.invisible) continue;
    if (v.quantum && v.quantum_observed)        glColor4f(0.65f, 0.1f, 1.0f, 1.0f);
    else if (v.quantum)                         glColor4f(0.5f, 0.1f, 0.8f, 0.65f);
    else if (v.teleporting)                     glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    else if (v.reflective)                      glColor4f(0.3f, 0.9f, 1.0f, 0.9f);
    else if (v.invincible)                      glColor4f(0.8f, 0.8f, 0.8f, 0.8f);
    else if (v.armoured)                        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    else                                        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
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

  // --- Crack pass for tough asteroids ---
  // Tough asteroids start with 1 crack; each subsequent hit reveals one more
  // (vertex → jittered midpoint → halfway-to-centre). Max 5 cracks visible.
  // crack_t and crack_perp are rotation-invariant fractions; reconstruct world coords
  // from the already-rotated dvx/dvy vertex offsets.
  if (!is_minimap) {
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    for (size_t ai = 0; ai < verts.size(); ++ai) {
      AsteroidVerts const &v = verts[ai];
      if (!v.tough || v.invisible) continue;
      int hits_taken = 6 - v.health; // health=5 → 1 crack at full health, +1 per hit
      glColor4f(0.7f, 0.7f, 0.7f, 1.0f); // grey crack on normal-coloured body
      for (int k = 0; k < hits_taken; k++) {
        int vi = v.crack_vertex[k];
        if (vi >= v.segs) vi = v.segs - 1;
        float vx = v.dvx[vi], vy = v.dvy[vi]; // vertex relative to centre
        float len = sqrtf(vx * vx + vy * vy);
        if (len < 1e-6f) continue;
        // Point along the straight vertex→centre line at parameter t
        float t = v.crack_t[k];
        float lx = vx * (1.0f - t);
        float ly = vy * (1.0f - t);
        // Perpendicular jitter (crack_perp is a fraction of vertex distance)
        float perp_dist = v.crack_perp[k] * len;
        float mx = lx + (-vy / len) * perp_dist;
        float my = ly + ( vx / len) * perp_dist;
        // Two segments: vertex → midpoint → halfway-to-centre (half length)
        float half_mx = vx + (mx - vx) * 0.5f;
        float half_my = vy + (my - vy) * 0.5f;
        float half_ex = vx * 0.5f;
        float half_ey = vy * 0.5f;
        glVertex2f(v.cx + vx,      v.cy + vy);
        glVertex2f(v.cx + half_mx, v.cy + half_my);
        glVertex2f(v.cx + half_mx, v.cy + half_my);
        glVertex2f(v.cx + half_ex, v.cy + half_ey);
      }
    }
    glEnd();
  }

  // --- Armour edge indicator pass ---
  // For each polygon edge whose midpoint falls within the ±75° shield arc,
  // redraw that edge scaled slightly outward in orange.
  if (!is_minimap) {
    const float arc_cos = -0.5f;  // cos(120°) = 2/3 coverage — must match collision threshold in ship.cpp
    const float scale   = 1.18f;  // draw outside the polygon surface
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    for (size_t ai = 0; ai < verts.size(); ++ai) {
      AsteroidVerts const &v = verts[ai];
      if (!v.armoured || v.invisible) continue;
      float adx = cosf(v.armour_angle), ady = sinf(v.armour_angle);
      glColor4f(0.3f, 0.9f, 1.0f, 0.9f);
      for (int i = 0; i < v.segs; i++) {
        int j = (i + 1) % v.segs;
        // Use edge midpoint angle to decide if this edge is in the shield arc
        float mx = (v.dvx[i] + v.dvx[j]) * 0.5f;
        float my = (v.dvy[i] + v.dvy[j]) * 0.5f;
        float mlen = sqrtf(mx * mx + my * my);
        if (mlen < 1e-6f) continue;
        if ((mx * adx + my * ady) / mlen < arc_cos) continue;
        glVertex2f(v.cx + v.dvx[i] * scale, v.cy + v.dvy[i] * scale);
        glVertex2f(v.cx + v.dvx[j] * scale, v.cy + v.dvy[j] * scale);
      }
    }
    glEnd();
  }

  // --- Inner indicator pass for teleporting asteroids ---
  if (!is_minimap) {
    glLineWidth(2.0f);
    for (size_t ai = 0; ai < verts.size(); ++ai) {
      AsteroidVerts const &v = verts[ai];
      if (!v.teleporting) continue;
      float r = v.radius * 0.45f; // indicator size relative to asteroid
      if (v.teleport_vulnerable) {
        // Draw a circle inside to show vulnerability
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_LINE_LOOP);
        const int circle_segs = 14;
        for (int k = 0; k < circle_segs; k++) {
          float a = k * 2.0f * (float)M_PI / circle_segs;
          glVertex2f(v.cx + r * cosf(a), v.cy + r * sinf(a));
        }
        glEnd();
      } else {
        // Draw a filled triangle arrow pointing in teleport_angle direction
        // Triangle: tip pointing forward, base behind
        float tip_x  = v.cx + r * cosf(v.teleport_angle);
        float tip_y  = v.cy + r * sinf(v.teleport_angle);
        float base_r = r * 0.55f;
        float back_angle = v.teleport_angle + (float)M_PI;
        float perp = v.teleport_angle + (float)M_PI / 2.0f;
        float bl_x = v.cx + base_r * cosf(back_angle) + base_r * 0.6f * cosf(perp);
        float bl_y = v.cy + base_r * sinf(back_angle) + base_r * 0.6f * sinf(perp);
        float br_x = v.cx + base_r * cosf(back_angle) - base_r * 0.6f * cosf(perp);
        float br_y = v.cy + base_r * sinf(back_angle) - base_r * 0.6f * sinf(perp);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glVertex2f(tip_x, tip_y);
        glVertex2f(bl_x, bl_y);
        glVertex2f(br_x, br_y);
        glEnd();
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(tip_x, tip_y);
        glVertex2f(bl_x, bl_y);
        glVertex2f(br_x, br_y);
        glEnd();
      }
    }
  }

  // --- Debris from alive teleporting asteroids (teleport ghost effect) ---
  if (!is_minimap) {
    static float tp_flicker[64];
    static bool tp_flicker_init = false;
    static int tp_flicker_idx = 0;
    if (!tp_flicker_init) {
      for (int i = 0; i < 64; i++)
        tp_flicker[i] = rand() / (float)RAND_MAX;
      tp_flicker_init = true;
    }
    bool any_debris = false;
    for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
      if ((*it)->teleporting && !(*it)->debris.empty()) { any_debris = true; break; }
    }
    if (any_debris) {
      glPointSize(3.0f);
      glBegin(GL_POINTS);
      for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
        Asteroid const *a = *it;
        if (!a->teleporting) continue;
        for (auto const &d : a->debris) {
          float alive = d.aliveness();
          float alpha = tp_flicker[tp_flicker_idx++ % 64] * alive / 2.0f + alive / 2.0f;
          // Orange/yellow debris for teleport-ready, purple for post-teleport
          if (a->teleport_vulnerable)
            glColor4f(0.8f, 0.2f, 1.0f, alpha);
          else
            glColor4f(1.0f, 0.6f, 0.1f, alpha);
          glVertex2fv(d.position);
        }
      }
      glEnd();
    }
  }

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
