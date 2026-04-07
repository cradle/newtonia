#ifndef GL_TRAIL_H
#define GL_TRAIL_H

#include "gl_compat.h"
#include "mesh.h"

#include "glship.h"
#include <vector>

class GLShip;

// Compact per-point storage replacing the heavy Particle/Object hierarchy.
// Stores only what the trail needs: world position, velocity, and lifetime.
struct TrailPoint {
  float x, y;
  float vx, vy;
  float time_left, ttl;

  TrailPoint(float x, float y, float vx, float vy, float ttl)
    : x(x), y(y), vx(vx), vy(vy), time_left(ttl), ttl(ttl) {}

  void step(float delta) {
    x += vx * delta;
    y += vy * delta;
    time_left -= delta;
  }

  bool is_alive() const { return time_left > 0.0f; }
  float aliveness() const { return time_left / ttl; }
};

class GLTrail {
public:
  GLTrail(GLShip* ship,
          float deviation = 0.05,
          Point offset = Point(),
          float speed = 0.25,
          float rotation = 0.0,
          int type = THRUSTING,
          float life = 250.0);
  virtual ~GLTrail();
  void draw();
  void step(float delta);

  //TODO: Would want constructor to take TYPE type = THRUSTING, but doesn't work
  enum TYPE {
    THRUSTING = 1,
    REVERSING = 2,
    LEFT = 4,
    RIGHT = 8,
    ALWAYS = 16
  };

private:
  void add();

  static const int add_interval = 15;
  int last_add_time;
  int type;
  GLShip* ship;
  Point offset;
  float deviation, rotation, speed, life, point_size;

  std::vector<TrailPoint> trail;
  Mesh mesh_;
};

#endif
