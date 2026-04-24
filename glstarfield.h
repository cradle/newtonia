#ifndef GLSTARFIELD_H
#define GLSTARFIELD_H

#include "point.h"

#include "gl_compat.h"
#include "mesh.h"

#include <vector>

class GLStarfield {
public:
  GLStarfield(Point const size, float density_scale = 1.0f);
  virtual ~GLStarfield();
  GLStarfield(GLStarfield&&) = default;
  GLStarfield& operator=(GLStarfield&&) = default;

  void draw_rear(Point const viewpoint) const;
  void draw_front(Point const viewpoint) const;

  // Draw rear stars near (cx, cy) at radially shifted positions, for the
  // invisible asteroid lensing effect.  Must be called between glPushMatrix /
  // glPopMatrix with the same tile transform that was used to draw the stars.
  struct StarPoint {
    float x, y, z;
    float r, g, b, a;
  };

  void draw_stars_near(float cx, float cy, float radius) const;
  void draw_front_stars_near(float cx, float cy, float radius) const;

private:
  void rebuild_lens_cache(float cx, float cy, float radius) const;

  std::vector<StarPoint> rear_stars;
  std::vector<StarPoint> front_stars;

  std::vector<Mesh*> layer_meshes; // NUM_REAR_LAYERS + 1 rear + NUM_FRONT_LAYERS front
  static const int NUM_REAR_LAYERS;
  static const int NUM_FRONT_LAYERS;
  static const float STAR_DENSITY;

  // Cached lensed-star meshes; rebuilt only when lens position/radius changes.
  // Since the black hole is stationary this is effectively a one-time build.
  mutable float lens_cx_ = 0.0f, lens_cy_ = 0.0f, lens_radius_ = -1.0f;
  mutable Mesh lensed_rear_mesh_, lensed_front_mesh_;
};

#endif
