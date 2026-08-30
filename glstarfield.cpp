#include "glstarfield.h"

#include "gl_compat.h"
#include "mesh.h"

#include <math.h>

const int GLStarfield::NUM_REAR_LAYERS = 10;
const int GLStarfield::NUM_FRONT_LAYERS = 5;
const float GLStarfield::STAR_DENSITY = 0.000015;
// Layer z is (i - NUM_REAR_LAYERS) * 100 (see the constructor), so the
// deepest rear layer sits NUM_REAR_LAYERS * 100 behind the play plane.
const float GLStarfield::REAR_DEPTH = GLStarfield::NUM_REAR_LAYERS * 100.0f;

// World-space star radius per unit of camera distance. The stars were
// GL_POINTS at 2*Typer::scale px until the maintainer's Linux laptop started
// dropping the layer draws whole every few seconds (2026-08-30 — the same
// driver point-size lottery as the 2026-08-23 debris saga; CLAUDE.md
// "Particles"). As triangles the size lives in the geometry, so each layer's
// quad is scaled by its distance from the camera to project at the size the
// fixed-pixel points had: 0.003 * dist reproduces the old 2 px at the
// 800x600 reference window under both cameras (game: z=1000, fov ~85; menu
// and lobby: z=0, fov 90), and tracks the window like every other
// world-space conversion.
const float GLStarfield::STAR_RADIUS_PER_DIST = 0.003f;

// Star count scales with world AREA and the late-game world grows 3000x3000
// per generation (gen >= 14), while each star is now six vertices instead of
// one — unbounded, the per-layer VBOs would reach hundreds of MB by gen ~30
// (code review, 2026-08-30). The cap binds from roughly gen 19 (world
// ~16500+ at full star density); by then at most a couple of the 3x3 world
// tiles ever pass the screen cull, so the visible sky thins gradually
// instead of the build and draw cost growing without limit.
static const int MAX_STARS_PER_LAYER = 4096;

GLStarfield::GLStarfield(Point const size, float density_scale, float camera_z)
    : camera_z_(camera_z) {
  int total = NUM_REAR_LAYERS + 1 + NUM_FRONT_LAYERS;
  layer_meshes.resize(total);

  int red, green;
  for(int i = 0; i < total; i++) {
    MeshBuilder mb;
    mb.begin(GL_TRIANGLES);

    float z_f = (float)((i - NUM_REAR_LAYERS) * 100);
    // Layers behind the camera (the menu's front layers) are clipped by the
    // projection anyway; a zero radius keeps their quads degenerate.
    float dist = camera_z_ - z_f;
    float star_r = STAR_RADIUS_PER_DIST * (dist > 0.0f ? dist : 0.0f);

    int num_stars = (int)(size.x()*size.y()*STAR_DENSITY*density_scale);
    if (num_stars > MAX_STARS_PER_LAYER) num_stars = MAX_STARS_PER_LAYER;
    for(int j = 0; j < num_stars; j++) {
      red = rand()%100;
      green = red > 0 ? rand()%red : 0;
      float r_f = red / 100.0f;
      float g_f = green / 100.0f;
      float b_f = rand()%100 / 100.0f;
      float a_f = rand()%40 / 100.0f + 0.6f;
      float x_f = (float)(rand()%(int)size.x());
      float y_f = (float)(rand()%(int)size.y());
      mb.color(r_f, g_f, b_f, a_f);
      mb.dot(x_f, y_f, star_r, z_f);
      // Store stars for lensing
      if (i <= NUM_REAR_LAYERS) {
        rear_stars.push_back({x_f, y_f, z_f, r_f, g_f, b_f, a_f});
      } else {
        front_stars.push_back({x_f, y_f, z_f, r_f, g_f, b_f, a_f});
      }
    }
    mb.end();
    layer_meshes[i] = new Mesh();
    layer_meshes[i]->upload(mb);
  }
}

GLStarfield::~GLStarfield() {
  for (Mesh* m : layer_meshes) delete m;
}

void GLStarfield::draw_rear(Point const viewpoint) const {
  for(int i = 0; i < NUM_REAR_LAYERS; i++) {
    layer_meshes[i]->draw();
  }
  layer_meshes[NUM_REAR_LAYERS]->draw();
}

void GLStarfield::draw_front(Point const viewpoint) const {
  for(int i = 0; i < NUM_FRONT_LAYERS; i++) {
    layer_meshes[NUM_REAR_LAYERS + 1 + i]->draw();
  }
}

static void build_lensed_mesh(MeshBuilder &mb,
                              std::vector<GLStarfield::StarPoint> const &stars,
                              float cx, float cy, float radius, float star_r) {
  float r2 = radius * radius;
  mb.begin(GL_TRIANGLES);
  for (const GLStarfield::StarPoint &s : stars) {
    float dx = s.x - cx;
    float dy = s.y - cy;
    float dist2 = dx * dx + dy * dy;
    if (dist2 >= r2) continue;
    float dist = sqrtf(dist2);
    float nx, ny, shift;
    if (dist > 0.001f) {
      nx = dx / dist;
      ny = dy / dist;
      float max_shift = radius - dist;
      shift = fminf(radius * 2.0f * (1.0f - dist / radius), max_shift);
    } else {
      nx = 1.0f; ny = 0.0f;
      shift = 0.0f;
    }
    mb.color(s.r, s.g, s.b, s.a);
    // Use z=0 so the shifted position projects at the same scale as the
    // void circle (which is also drawn at z=0).  Stars at positive z
    // would otherwise project further out on screen and escape the void.
    mb.dot(s.x + nx * shift, s.y + ny * shift, star_r, 0.0f);
  }
  mb.end();
}

void GLStarfield::rebuild_lens_cache(float cx, float cy, float radius) const {
  // Every lensed star draws at z=0 (see build_lensed_mesh), so one radius —
  // the z=0 plane's camera distance — sizes them all. Lensing only happens
  // in-game (camera_z 1000); a menu instance never gets here.
  float star_r = STAR_RADIUS_PER_DIST * camera_z_;
  MeshBuilder mb;
  build_lensed_mesh(mb, rear_stars, cx, cy, radius, star_r);
  lensed_rear_mesh_.upload(mb);
  mb.clear();
  build_lensed_mesh(mb, front_stars, cx, cy, radius, star_r);
  lensed_front_mesh_.upload(mb);
  lens_cx_ = cx; lens_cy_ = cy; lens_radius_ = radius;
}

void GLStarfield::draw_stars_near(float cx, float cy, float radius) const {
  if (cx != lens_cx_ || cy != lens_cy_ || radius != lens_radius_)
    rebuild_lens_cache(cx, cy, radius);
  lensed_rear_mesh_.draw();
}

void GLStarfield::draw_front_stars_near(float cx, float cy, float radius) const {
  if (cx != lens_cx_ || cy != lens_cy_ || radius != lens_radius_)
    rebuild_lens_cache(cx, cy, radius);
  lensed_front_mesh_.draw();
}
