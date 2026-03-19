#ifndef GLSTARFIELD_H
#define GLSTARFIELD_H

#include "point.h"

#include "gl_compat.h"

#include <vector>

class GLStarfield {
public:
  GLStarfield(Point const size);
  virtual ~GLStarfield();

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
  std::vector<StarPoint> rear_stars;
  std::vector<StarPoint> front_stars;

  GLuint point_layers;
  static const int NUM_REAR_LAYERS;
  static const int NUM_FRONT_LAYERS;
  static const float STAR_DENSITY;
};

#endif
