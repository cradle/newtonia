#include "glstarfield.h"

#include "gl_compat.h"
#include "mesh.h"
#include "typer.h"

#include <math.h>

const int GLStarfield::NUM_REAR_LAYERS = 10;
const int GLStarfield::NUM_FRONT_LAYERS = 5;
const float GLStarfield::STAR_DENSITY = 0.000015;

GLStarfield::GLStarfield(Point const size) {
  int total = NUM_REAR_LAYERS + 1 + NUM_FRONT_LAYERS;
  layer_meshes.resize(total);

  int red, green;
  for(int i = 0; i < total; i++) {
    MeshBuilder mb;
    mb.begin(GL_POINTS);

    int num_stars = (int)(size.x()*size.y()*STAR_DENSITY);
    for(int j = 0; j < num_stars; j++) {
      red = rand()%100;
      green = red > 0 ? rand()%red : 0;
      float r_f = red / 100.0f;
      float g_f = green / 100.0f;
      float b_f = rand()%100 / 100.0f;
      float a_f = rand()%40 / 100.0f + 0.6f;
      float x_f = (float)(rand()%(int)size.x());
      float y_f = (float)(rand()%(int)size.y());
      float z_f = (float)((i - NUM_REAR_LAYERS) * 100);
      mb.color(r_f, g_f, b_f, a_f);
      mb.vertex(x_f, y_f, z_f);
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
  float ps = 2.0f * Typer::scale;
  for(int i = 0; i < NUM_REAR_LAYERS; i++) {
    layer_meshes[i]->draw(ps);
  }
  layer_meshes[NUM_REAR_LAYERS]->draw(ps);
}

void GLStarfield::draw_front(Point const viewpoint) const {
  float ps = 2.0f * Typer::scale;
  for(int i = 0; i < NUM_FRONT_LAYERS; i++) {
    layer_meshes[NUM_REAR_LAYERS + 1 + i]->draw(ps);
  }
}

static void build_lensed_mesh(MeshBuilder &mb,
                              std::vector<GLStarfield::StarPoint> const &stars,
                              float cx, float cy, float radius) {
  float r2 = radius * radius;
  mb.begin(GL_POINTS);
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
    mb.vertex(s.x + nx * shift, s.y + ny * shift, 0.0f);
  }
  mb.end();
}

void GLStarfield::rebuild_lens_cache(float cx, float cy, float radius) const {
  MeshBuilder mb;
  build_lensed_mesh(mb, rear_stars, cx, cy, radius);
  lensed_rear_mesh_.upload(mb);
  mb.clear();
  build_lensed_mesh(mb, front_stars, cx, cy, radius);
  lensed_front_mesh_.upload(mb);
  lens_cx_ = cx; lens_cy_ = cy; lens_radius_ = radius;
}

void GLStarfield::draw_stars_near(float cx, float cy, float radius) const {
  if (cx != lens_cx_ || cy != lens_cy_ || radius != lens_radius_)
    rebuild_lens_cache(cx, cy, radius);
  lensed_rear_mesh_.draw(2.0f * Typer::scale);
}

void GLStarfield::draw_front_stars_near(float cx, float cy, float radius) const {
  if (cx != lens_cx_ || cy != lens_cy_ || radius != lens_radius_)
    rebuild_lens_cache(cx, cy, radius);
  lensed_front_mesh_.draw(2.0f * Typer::scale);
}
