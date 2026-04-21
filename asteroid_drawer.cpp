#include "asteroid_drawer.h"
#include "asteroid.h"
#include "typer.h"
#include "mesh.h"
#include <math.h>

#include "gl_compat.h"
#include "mat4.h"

const int AsteroidDrawer::number_of_segments = 7;

int AsteroidDrawer::seg_count(float radius) {
  int s = number_of_segments;
  if      (radius < 15)  s -= 2;
  else if (radius < 30)  s -= 1;
  else if (radius > 200) s += 2;
  return s;
}

void AsteroidDrawer::draw(Asteroid const *object, float direction, bool is_minimap) {
  if (object->alive && !object->invisible) {
    static MeshBuilder mb;
    static Mesh mesh;

    float cx  = object->position.x();
    float cy  = object->position.y();
    float r   = object->radius;
    float rot = object->rotation * (float)M_PI / 180.0f;
    int   sc  = seg_count(r);
    float step = 2.0f * (float)M_PI / sc;

    mb.clear();

    float fr, fg, fb, fa;
    if      (object->reflective)  { fr=0.0f; fg=0.4f; fb=0.5f; fa=0.6f; }
    else if (object->invincible)  { fr=0.5f; fg=0.5f; fb=0.5f; fa=0.5f; }
    else                          { fr=0.0f; fg=0.0f; fb=0.0f; fa=1.0f; }
    mb.begin(GL_TRIANGLES);
    mb.color(fr, fg, fb, fa);
    for (int i = 0; i < sc; i++) {
      int j = (i + 1) % sc;
      float ai = rot + i * step, aj = rot + j * step;
      mb.vertex(cx, cy);
      mb.vertex(cx + r * object->vertex_offsets[i] * cosf(ai),
                cy + r * object->vertex_offsets[i] * sinf(ai));
      mb.vertex(cx + r * object->vertex_offsets[j] * cosf(aj),
                cy + r * object->vertex_offsets[j] * sinf(aj));
    }
    mb.end();

    float or_, og, ob, oa;
    if      (object->reflective)  { or_=0.3f; og=0.9f; ob=1.0f; oa=0.9f; }
    else if (object->invincible)  { or_=0.8f; og=0.8f; ob=0.8f; oa=0.8f; }
    else                          { or_=1.0f; og=1.0f; ob=1.0f; oa=1.0f; }
    mb.begin(GL_LINES);
    mb.color(or_, og, ob, oa);
    for (int i = 0; i < sc; i++) {
      int j = (i + 1) % sc;
      float ai = rot + i * step, aj = rot + j * step;
      mb.vertex(cx + r * object->vertex_offsets[i] * cosf(ai),
                cy + r * object->vertex_offsets[i] * sinf(ai));
      mb.vertex(cx + r * object->vertex_offsets[j] * cosf(aj),
                cy + r * object->vertex_offsets[j] * sinf(aj));
    }
    mb.end();

    glLineWidth(is_minimap ? 1.0f : 2.5f);
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  } else if (!is_minimap) {
    draw_debris(object->debris);
    float tile_vp[16]; gles2_get_mvp(tile_vp);
    float val_vp[16];
    mat4_translate(val_vp, tile_vp, object->position.x(), object->position.y(), 0.0f);
    mat4_rotate_z(val_vp, val_vp, -direction);
    gles2_set_vp(val_vp);
    Typer::draw(0.0f, 0.0f, object->value, 18.0f / Typer::scale);
    gles2_set_vp(tile_vp);
  }
}

void AsteroidDrawer::draw_invisible_mask(Asteroid const *object, float x, float y) {
  static MeshBuilder mb;
  static Mesh mesh;

  int   segs = seg_count(object->radius);
  float step = 2.0f * (float)M_PI / segs;
  float rot  = object->rotation * (float)M_PI / 180.0f;
  float r    = object->radius;

  mb.clear();
  mb.begin(GL_TRIANGLES);
  mb.color(0.0f, 0.0f, 0.0f, 1.0f);
  for (int i = 0; i < segs; i++) {
    int j = (i + 1) % segs;
    float ai = rot + i * step, aj = rot + j * step;
    mb.vertex(x, y);
    mb.vertex(x + r * object->vertex_offsets[i] * cosf(ai),
              y + r * object->vertex_offsets[i] * sinf(ai));
    mb.vertex(x + r * object->vertex_offsets[j] * cosf(aj),
              y + r * object->vertex_offsets[j] * sinf(aj));
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

// ---- Cached per-asteroid vertex data ----------------------------------------

struct AsteroidVerts {
  float dvx[9], dvy[9];
  float cx, cy, dx, dy;
  int segs;
  float radius;
  bool invincible, invisible, reflective;
  bool teleporting, teleport_vulnerable;
  float teleport_angle;
  bool quantum, quantum_observed;
  bool tough;
  int health;
  int crack_vertex[5];
  float crack_t[5];
  float crack_perp[5];
  bool armoured;
  float armour_angle;
  bool phasing;
  bool phased;
  int  phase_timer;
};

void AsteroidDrawer::draw_batch(list<Asteroid*> const *objects,
                                list<Asteroid*> const *dead_objects,
                                float direction, bool is_minimap,
                                float wrap_x, float wrap_y) {
  // --- Pre-compute vertex data (trig once per asteroid) ---
  vector<AsteroidVerts> verts;
  verts.reserve(objects->size());
  for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
    Asteroid const *a = *it;
    AsteroidVerts v;
    v.cx  = a->position.x();
    v.cy  = a->position.y();
    float r   = a->radius;
    float rot = a->rotation * (float)M_PI / 180.0f;
    v.segs    = seg_count(r);
    float step = 2.0f * (float)M_PI / v.segs;
    for (int i = 0; i < v.segs; i++) {
      float angle = rot + i * step;
      float off   = a->vertex_offsets[i];
      v.dvx[i] = r * off * cosf(angle);
      v.dvy[i] = r * off * sinf(angle);
    }
    v.radius = r;
    v.dx = 0; v.dy = 0;
    if (wrap_x > 0) {
      if      (v.cx < r)               v.dx =  wrap_x;
      else if (v.cx + r > wrap_x)      v.dx = -wrap_x;
      if      (v.cy < r)               v.dy =  wrap_y;
      else if (v.cy + r > wrap_y)      v.dy = -wrap_y;
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
    v.phasing             = a->phasing;
    v.phased              = a->phased;
    v.phase_timer         = a->phase_timer;
    for (int k = 0; k < 5; k++) {
      v.crack_vertex[k] = a->crack_vertex[k];
      v.crack_t[k]      = a->crack_t[k];
      v.crack_perp[k]   = a->crack_perp[k];
    }
    verts.push_back(v);
  }

  // Shared streaming MeshBuilder and per-pass Mesh objects.
  static MeshBuilder mb;
  static Mesh mesh_fill, mesh_outline, mesh_cracks, mesh_armour,
              mesh_teleport, mesh_debris;

  // --- Fill pass (all asteroids in one GL_TRIANGLES group) ---
  mb.clear();
  mb.begin(GL_TRIANGLES);
  for (size_t ai = 0; ai < verts.size(); ++ai) {
    AsteroidVerts const &v = verts[ai];
    float r, g, b, a;
    if      (v.invisible)                          { r=0.0f; g=0.0f; b=0.0f; a=1.0f; }
    else if (v.phasing && v.phased)                { r=0.0f; g=0.4f; b=0.5f; a=0.6f; }
    else if (v.phasing)                            { r=0.0f; g=0.0f; b=0.0f; a=1.0f; }
    else if (v.quantum && v.quantum_observed)      { r=0.15f;g=0.0f; b=0.35f;a=0.85f;}
    else if (v.quantum)                            { r=0.1f; g=0.0f; b=0.25f;a=0.6f; }
    else if (v.teleporting || v.armoured)          { r=0.0f; g=0.0f; b=0.0f; a=1.0f; }
    else if (v.reflective)                         { r=0.0f; g=0.4f; b=0.5f; a=0.6f; }
    else if (v.invincible)                         { r=0.5f; g=0.5f; b=0.5f; a=0.5f; }
    else                                           { r=0.0f; g=0.0f; b=0.0f; a=1.0f; }
    mb.color(r, g, b, a);
    for (int wi = 0; wi < (v.dx != 0 ? 2 : 1); wi++) {
      for (int wj = 0; wj < (v.dy != 0 ? 2 : 1); wj++) {
        float wcx = v.cx + wi * v.dx;
        float wcy = v.cy + wj * v.dy;
        for (int i = 0; i < v.segs; i++) {
          int j = (i + 1) % v.segs;
          mb.vertex(wcx,              wcy);
          mb.vertex(wcx + v.dvx[i],  wcy + v.dvy[i]);
          mb.vertex(wcx + v.dvx[j],  wcy + v.dvy[j]);
        }
      }
    }
  }
  mb.end();
  mesh_fill.upload(mb, GL_DYNAMIC_DRAW);
  mesh_fill.draw();

  // --- Outline pass (non-invisible asteroids, GL_LINES) ---
  mb.clear();
  mb.begin(GL_LINES);
  for (size_t ai = 0; ai < verts.size(); ++ai) {
    AsteroidVerts const &v = verts[ai];
    if (v.invisible) continue;
    float r, g, b, a;
    if (v.phasing && v.phased) {
      // Ghost: reflective colours (bullets bounce off); brighter when about to re-solidify
      bool warning = (v.phase_timer < 400) && ((v.phase_timer % 200) < 100);
      r=0.3f; g=0.9f; b=1.0f; a = warning ? 1.0f : 0.9f;
    } else if (v.phasing) {
      // Solid: killable colours; flashes when window is closing
      bool warning = (v.phase_timer < 500) && ((v.phase_timer % 200) < 100);
      r=1.0f; g=1.0f; b=1.0f; a = warning ? 0.4f : 1.0f;
    } else if (v.quantum && v.quantum_observed) { r=0.65f;g=0.1f; b=1.0f; a=1.0f; }
    else if (v.quantum)                         { r=0.5f; g=0.1f; b=0.8f; a=0.65f;}
    else if (v.teleporting)                     { r=1.0f; g=1.0f; b=1.0f; a=1.0f; }
    else if (v.reflective)                      { r=0.3f; g=0.9f; b=1.0f; a=0.9f; }
    else if (v.invincible)                      { r=0.8f; g=0.8f; b=0.8f; a=0.8f; }
    else                                        { r=1.0f; g=1.0f; b=1.0f; a=1.0f; }
    mb.color(r, g, b, a);
    for (int wi = 0; wi < (v.dx != 0 ? 2 : 1); wi++) {
      for (int wj = 0; wj < (v.dy != 0 ? 2 : 1); wj++) {
        float wcx = v.cx + wi * v.dx;
        float wcy = v.cy + wj * v.dy;
        for (int i = 0; i < v.segs; i++) {
          int j = (i + 1) % v.segs;
          mb.vertex(wcx + v.dvx[i], wcy + v.dvy[i]);
          mb.vertex(wcx + v.dvx[j], wcy + v.dvy[j]);
        }
      }
    }
  }
  mb.end();
  glLineWidth(is_minimap ? 1.0f : 2.5f);
  mesh_outline.upload(mb, GL_DYNAMIC_DRAW);
  mesh_outline.draw();

  if (!is_minimap) {
    // --- Crack pass (tough asteroids) ---
    mb.clear();
    mb.begin(GL_LINES);
    mb.color(0.7f, 0.7f, 0.7f, 1.0f);
    for (size_t ai = 0; ai < verts.size(); ++ai) {
      AsteroidVerts const &v = verts[ai];
      if (!v.tough || v.invisible) continue;
      int hits_taken = 6 - v.health;
      for (int k = 0; k < hits_taken; k++) {
        int vi = v.crack_vertex[k];
        if (vi >= v.segs) vi = v.segs - 1;
        float vx = v.dvx[vi], vy = v.dvy[vi];
        float len = sqrtf(vx * vx + vy * vy);
        if (len < 1e-6f) continue;
        float t  = v.crack_t[k];
        float lx = vx * (1.0f - t);
        float ly = vy * (1.0f - t);
        float perp_dist = v.crack_perp[k] * len;
        float mx = lx + (-vy / len) * perp_dist;
        float my = ly + ( vx / len) * perp_dist;
        float half_mx = vx + (mx - vx) * 0.5f;
        float half_my = vy + (my - vy) * 0.5f;
        float half_ex = vx * 0.5f;
        float half_ey = vy * 0.5f;
        mb.vertex(v.cx + vx,      v.cy + vy);
        mb.vertex(v.cx + half_mx, v.cy + half_my);
        mb.vertex(v.cx + half_mx, v.cy + half_my);
        mb.vertex(v.cx + half_ex, v.cy + half_ey);
      }
    }
    mb.end();
    glLineWidth(1.5f);
    mesh_cracks.upload(mb, GL_DYNAMIC_DRAW);
    mesh_cracks.draw();

    // --- Armour edge indicator pass ---
    mb.clear();
    mb.begin(GL_LINES);
    mb.color(0.3f, 0.9f, 1.0f, 0.9f);
    const float scale = 1.18f;
    for (size_t ai = 0; ai < verts.size(); ++ai) {
      AsteroidVerts const &v = verts[ai];
      if (!v.armoured || v.invisible) continue;
      float adx = cosf(v.armour_angle), ady = sinf(v.armour_angle);
      float b1x = cosf(v.armour_angle + 2.0f * (float)M_PI / 3.0f);
      float b1y = sinf(v.armour_angle + 2.0f * (float)M_PI / 3.0f);
      float b2x = cosf(v.armour_angle - 2.0f * (float)M_PI / 3.0f);
      float b2y = sinf(v.armour_angle - 2.0f * (float)M_PI / 3.0f);
      for (int i = 0; i < v.segs; i++) {
        int j = (i + 1) % v.segs;
        float ax = v.dvx[i], ay = v.dvy[i];
        float bx = v.dvx[j], by = v.dvy[j];
        float la = sqrtf(ax*ax + ay*ay);
        float lb = sqrtf(bx*bx + by*by);
        bool a_in = (la > 1e-6f) && (ax*adx + ay*ady > -0.5f * la);
        bool b_in = (lb > 1e-6f) && (bx*adx + by*ady > -0.5f * lb);
        if (!a_in && !b_in) continue;
        float draw_ax = ax, draw_ay = ay;
        float draw_bx = bx, draw_by = by;
        if (!a_in || !b_in) {
          float edx = bx - ax, edy = by - ay;
          float t = -1.0f;
          float d1 = b1x * edy - b1y * edx;
          if (fabsf(d1) > 1e-10f) {
            float tc = -(b1x * ay - b1y * ax) / d1;
            if (tc >= 0.0f && tc <= 1.0f) t = tc;
          }
          if (t < 0.0f) {
            float d2 = b2x * edy - b2y * edx;
            if (fabsf(d2) > 1e-10f) {
              float tc = -(b2x * ay - b2y * ax) / d2;
              if (tc >= 0.0f && tc <= 1.0f) t = tc;
            }
          }
          if (t < 0.0f) continue;
          if (!a_in) { draw_ax = ax + t*edx; draw_ay = ay + t*edy; }
          else        { draw_bx = ax + t*edx; draw_by = ay + t*edy; }
        }
        mb.vertex(v.cx + draw_ax * scale, v.cy + draw_ay * scale);
        mb.vertex(v.cx + draw_bx * scale, v.cy + draw_by * scale);
      }
    }
    mb.end();
    glLineWidth(3.0f);
    mesh_armour.upload(mb, GL_DYNAMIC_DRAW);
    mesh_armour.draw();

    // --- Teleport indicators (circles and arrows) ---
    mb.clear();
    for (size_t ai = 0; ai < verts.size(); ++ai) {
      AsteroidVerts const &v = verts[ai];
      if (!v.teleporting) continue;
      float r = v.radius * 0.45f;
      if (v.teleport_vulnerable) {
        mb.begin(GL_LINE_LOOP);
        mb.color(1.0f, 1.0f, 1.0f, 1.0f);
        const int circle_segs = 14;
        for (int k = 0; k < circle_segs; k++) {
          float a = k * 2.0f * (float)M_PI / circle_segs;
          mb.vertex(v.cx + r * cosf(a), v.cy + r * sinf(a));
        }
        mb.end();
      } else {
        float tip_x  = v.cx + r * cosf(v.teleport_angle);
        float tip_y  = v.cy + r * sinf(v.teleport_angle);
        float base_r = r * 0.55f;
        float back_angle = v.teleport_angle + (float)M_PI;
        float perp       = v.teleport_angle + (float)M_PI / 2.0f;
        float bl_x = v.cx + base_r * cosf(back_angle) + base_r * 0.6f * cosf(perp);
        float bl_y = v.cy + base_r * sinf(back_angle) + base_r * 0.6f * sinf(perp);
        float br_x = v.cx + base_r * cosf(back_angle) - base_r * 0.6f * cosf(perp);
        float br_y = v.cy + base_r * sinf(back_angle) - base_r * 0.6f * sinf(perp);
        mb.begin(GL_TRIANGLES);
        mb.color(1.0f, 1.0f, 1.0f, 1.0f);
        mb.vertex(tip_x, tip_y);
        mb.vertex(bl_x, bl_y);
        mb.vertex(br_x, br_y);
        mb.end();
        mb.begin(GL_LINE_LOOP);
        mb.color(1.0f, 1.0f, 1.0f, 1.0f);
        mb.vertex(tip_x, tip_y);
        mb.vertex(bl_x, bl_y);
        mb.vertex(br_x, br_y);
        mb.end();
      }
    }
    glLineWidth(2.0f);
    mesh_teleport.upload(mb, GL_DYNAMIC_DRAW);
    mesh_teleport.draw();

    // --- Teleport debris (alive teleporting asteroids) ---
    mb.clear();
    {
      static float tp_flicker[64];
      static bool  tp_flicker_init = false;
      static int   tp_flicker_idx  = 0;
      if (!tp_flicker_init) {
        for (int i = 0; i < 64; i++) tp_flicker[i] = rand() / (float)RAND_MAX;
        tp_flicker_init = true;
      }
      mb.begin(GL_POINTS);
      for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
        Asteroid const *a = *it;
        if (!a->teleporting) continue;
        for (auto const &d : a->debris) {
          float alive = d.aliveness();
          float alpha = tp_flicker[tp_flicker_idx++ % 64] * alive / 2.0f + alive / 2.0f;
          if (a->teleport_vulnerable)
            mb.color(0.8f, 0.2f, 1.0f, alpha);
          else
            mb.color(1.0f, 0.6f, 0.1f, alpha);
          mb.vertex(d.position.x(), d.position.y());
        }
      }
      mb.end();
    }
    mesh_debris.upload(mb, GL_DYNAMIC_DRAW);
    mesh_debris.draw(3.0f);

    // --- Dead asteroid debris + score text ---
    {
      static float flicker[64];
      static bool  flicker_init = false;
      static int   flicker_idx  = 0;
      if (!flicker_init) {
        for (int i = 0; i < 64; i++) flicker[i] = rand() / (float)RAND_MAX;
        flicker_init = true;
      }
      static Mesh mesh_dead_debris;
      mb.clear();
      mb.begin(GL_POINTS);
      for (list<Asteroid*>::const_iterator it = dead_objects->begin(); it != dead_objects->end(); ++it) {
        Asteroid const *a = *it;
        for (auto d = a->debris.begin(); d != a->debris.end(); ++d) {
          float alive = d->aliveness();
          float alpha = flicker[flicker_idx++ % 64] * alive / 2.0f + alive / 2.0f;
          mb.color(1.0f, 1.0f, 1.0f, alpha);
          mb.vertex(d->position.x(), d->position.y());
        }
      }
      mb.end();
      mesh_dead_debris.upload(mb, GL_DYNAMIC_DRAW);
      mesh_dead_debris.draw(3.0f);
    }

    float tile_vp[16]; gles2_get_mvp(tile_vp);
    for (list<Asteroid*>::const_iterator it = dead_objects->begin(); it != dead_objects->end(); ++it) {
      Asteroid const *a = *it;
      float val_vp[16];
      mat4_translate(val_vp, tile_vp, a->position.x(), a->position.y(), 0.0f);
      mat4_rotate_z(val_vp, val_vp, -direction);
      gles2_set_vp(val_vp);
      Typer::draw(0.0f, 0.0f, a->value, 18.0f / Typer::scale);
    }
    gles2_set_vp(tile_vp);
  }
}

void AsteroidDrawer::draw_debris(vector<Particle> const &debris) {
  static float flicker[64];
  static bool  flicker_init = false;
  static int   flicker_idx  = 0;
  if (!flicker_init) {
    for (int i = 0; i < 64; i++) flicker[i] = rand() / (float)RAND_MAX;
    flicker_init = true;
  }
  static MeshBuilder mb;
  static Mesh mesh;
  mb.clear();
  mb.begin(GL_POINTS);
  for (auto d = debris.begin(); d != debris.end(); ++d) {
    float alive = d->aliveness();
    float alpha = flicker[flicker_idx++ % 64] * alive / 2.0f + alive / 2.0f;
    mb.color(1.0f, 1.0f, 1.0f, alpha);
    mb.vertex(d->position.x(), d->position.y());
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw(3.0f);
}
