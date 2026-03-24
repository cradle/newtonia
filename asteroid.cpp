#include "asteroid.h"
#include "wrapped_point.h"

#include <list>
#include <cmath>

using namespace std;

const int Asteroid::max_speed = 5;
const int Asteroid::max_rotation = 15;
int Asteroid::num_killable = 0;

const int Asteroid::radius_variation = 220;
const int Asteroid::minimum_radius = 20;

Mix_Chunk * Asteroid::explode_sound = NULL;
Mix_Chunk * Asteroid::thud_sound = NULL;

const int Asteroid::max_radius = Asteroid::radius_variation + Asteroid::minimum_radius;

Asteroid::Asteroid(bool invincible, bool invisible, bool reflective) : CompositeObject(), killed(false) {
  position = WrappedPoint();
  this->reflective = reflective;
  if(reflective) invincible = true;
  if(invincible) {
    radius = rand()%radius_variation + minimum_radius;
  } else if(invisible) {
    radius = max_radius / 2 + rand() % (max_radius / 4 + 1);
  } else {
    radius = (rand()%radius_variation + minimum_radius) * 0.5;
  }
  rotation_speed = (rand()%max_rotation-max_rotation/2)/radius;
  if (invisible) {
    // Large radius + random sign can yield near-zero rotation. Ensure a
    // perceptible minimum of ~20 deg/s regardless of size.
    float min_rot = 4.0f / radius;
    if (rotation_speed > -min_rot && rotation_speed < min_rot)
      rotation_speed = (rand() % 2) ? min_rot : -min_rot;
  }
  velocity = Point(rand()-RAND_MAX/2, rand()-RAND_MAX/2).normalized()*max_speed/radius;
  value = float(radius/(radius_variation + minimum_radius)) * 100.0f;
  for (int i = 0; i < 9; i++)
    vertex_offsets[i] = 0.7f + (rand() / (float)RAND_MAX) * 0.6f;
  max_vertex_offset = vertex_offsets[0];
  for (int i = 1; i < 9; i++)
    if (vertex_offsets[i] > max_vertex_offset) max_vertex_offset = vertex_offsets[i];
  children_added = false;
  this->invincible = invincible;
  this->invisible = invisible;
  if(!invincible) {
    num_killable++;
  }
  if(explode_sound == NULL) {
    explode_sound = Mix_LoadWAV("audio/explode.wav");
    if(explode_sound == NULL) {
      std::cout << "Unable to load explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(thud_sound == NULL) {
    thud_sound = Mix_LoadWAV("audio/thud.wav");
    if(thud_sound == NULL) {
        std::cout << "Unable to load thud.wav (" << Mix_GetError() << ")" << std::endl;
    } else {
        //Mix_VolumeChunk(thud_sound, MIX_MAX_VOLUME/2); // Todo:  distance volume
    }
  }
}

Asteroid::~Asteroid() {
  if(!killed && !invincible) {
    num_killable--;
  }
}

void Asteroid::free_sounds() {
  if(explode_sound != NULL) { Mix_FreeChunk(explode_sound); explode_sound = NULL; }
  if(thud_sound != NULL)    { Mix_FreeChunk(thud_sound);    thud_sound    = NULL; }
}

Asteroid::Asteroid(Asteroid const *mother) {
  radius = mother->radius/2.0f;
  rotation_speed = (rand()%6-3)/radius;
  velocity = Point(rand()-RAND_MAX/2, rand()-RAND_MAX/2).normalized()*max_speed/radius;
  position = mother->position + velocity.normalized() * radius;
  value = float(radius/(radius_variation + minimum_radius)) * 100.0f;
  value += mother->value;
  for (int i = 0; i < 9; i++)
    vertex_offsets[i] = 0.7f + (rand() / (float)RAND_MAX) * 0.6f;
  max_vertex_offset = vertex_offsets[0];
  for (int i = 1; i < 9; i++)
    if (vertex_offsets[i] > max_vertex_offset) max_vertex_offset = vertex_offsets[i];
  children_added = false;
  invisible = false;
  reflective = false;
  if(!invincible) {
    killed = false;
    num_killable++;
  }
}

// Returns the outward normal of the polygon edge the bullet crossed.
// Primary criterion: closest edge to 'entry' (the back-traced surface point).
// Tiebreaker for corners (equidistant edges): edge whose normal most opposes
// incoming_dir, i.e. the face the bullet was actually heading toward.
Point Asteroid::surface_normal(Point entry, Point incoming_dir) const {
  int segs = 7;
  if      (radius < 15)  segs = 5;
  else if (radius < 30)  segs = 6;
  else if (radius > 200) segs = 9;

  float rot = rotation * (float)M_PI / 180.0f;
  const float step = 2.0f * (float)M_PI / segs;

  float vx[9], vy[9];
  for (int i = 0; i < segs; i++) {
    float angle = rot + i * step;
    vx[i] = position.x() + radius * vertex_offsets[i] * cosf(angle);
    vy[i] = position.y() + radius * vertex_offsets[i] * sinf(angle);
  }

  Point inc = incoming_dir.normalized();
  float best_dist = 1e30f;
  float best_align = 2.0f;
  float best_nx = 0.0f, best_ny = 1.0f;

  for (int i = 0; i < segs; i++) {
    int j = (i + 1) % segs;
    float dx = vx[j] - vx[i], dy = vy[j] - vy[i];
    float len2 = dx * dx + dy * dy;
    if (len2 < 1e-12f) continue;

    // Closest point on this edge segment to entry
    float t = ((entry.x() - vx[i]) * dx + (entry.y() - vy[i]) * dy) / len2;
    t = fmaxf(0.0f, fminf(1.0f, t));
    float cx = vx[i] + t * dx, cy = vy[i] + t * dy;
    float dist = sqrtf((entry.x() - cx) * (entry.x() - cx) +
                       (entry.y() - cy) * (entry.y() - cy));

    // Outward normal: perpendicular to edge, pointing away from asteroid center
    float len = sqrtf(len2);
    float nx = -dy / len, ny = dx / len;
    float mid_x = (vx[i] + vx[j]) * 0.5f - position.x();
    float mid_y = (vy[i] + vy[j]) * 0.5f - position.y();
    if (nx * mid_x + ny * mid_y < 0.0f) { nx = -nx; ny = -ny; }

    float align = nx * inc.x() + ny * inc.y();

    // Pick closest edge; break ties (within 1px) by most-opposing normal
    if (dist < best_dist - 1.0f || (dist < best_dist + 1.0f && align < best_align)) {
      best_dist  = dist;
      best_align = align;
      best_nx = nx; best_ny = ny;
    }
  }
  return Point(best_nx, best_ny);
}

// Test whether the segment [a,b] crosses any edge of the asteroid polygon.
// t_hit is set to the parameter of the earliest crossing (0=a, 1=b).
bool Asteroid::segment_hit(Point a, Point b, float &t_hit) const {
  int segs = 7;
  if      (radius < 15)  segs = 5;
  else if (radius < 30)  segs = 6;
  else if (radius > 200) segs = 9;

  float rot = rotation * (float)M_PI / 180.0f;
  const float ang_step = 2.0f * (float)M_PI / segs;

  float vx[9], vy[9];
  for (int i = 0; i < segs; i++) {
    float angle = rot + i * ang_step;
    vx[i] = position.x() + radius * vertex_offsets[i] * cosf(angle);
    vy[i] = position.y() + radius * vertex_offsets[i] * sinf(angle);
  }

  float dx = b.x() - a.x(), dy = b.y() - a.y();
  t_hit = 2.0f;
  bool hit = false;

  for (int i = 0; i < segs; i++) {
    int j = (i + 1) % segs;
    float ex = vx[j] - vx[i], ey = vy[j] - vy[i];
    float denom = dx * ey - dy * ex;
    if (fabsf(denom) < 1e-10f) continue; // parallel
    float fx = vx[i] - a.x(), fy = vy[i] - a.y();
    float t = (fx * ey - fy * ex) / denom; // parameter along bullet segment
    float u = (fx * dy - fy * dx) / denom; // parameter along polygon edge
    if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f && t < t_hit) {
      t_hit = t;
      hit = true;
    }
  }
  return hit;
}

bool Asteroid::contains(Point p, float r) const {
  float lx = p.x() - position.x();
  float ly = p.y() - position.y();

  // Match segment count to renderer (mirrors AsteroidDrawer::seg_count logic)
  int segs = 7;
  if      (radius < 15)  segs = 5;
  else if (radius < 30)  segs = 6;
  else if (radius > 200) segs = 9;

  float rot = rotation * (float)M_PI / 180.0f;
  const float step = 2.0f * (float)M_PI / segs;

  // Inflate each vertex outward by r (Minkowski expansion for circle vs polygon).
  // For bullets r≈1 this is negligible; for ships r=15 this accounts for the
  // ship's body so collision triggers when the ship circle touches the polygon.
  float vx[9], vy[9];
  for (int i = 0; i < segs; i++) {
    float angle = rot + i * step;
    vx[i] = (radius * vertex_offsets[i] + r) * cosf(angle);
    vy[i] = (radius * vertex_offsets[i] + r) * sinf(angle);
  }

  // Test each triangle (origin, vi, vi+1) — same fan used for rendering
  for (int i = 0; i < segs; i++) {
    int j = (i + 1) % segs;
    float d1 = vx[i] * ly - vy[i] * lx;
    float d2 = (vx[j]-vx[i]) * (ly-vy[i]) - (vy[j]-vy[i]) * (lx-vx[i]);
    float d3 = -vx[j] * (ly-vy[j]) + vy[j] * (lx-vx[j]);
    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    if (!(has_neg && has_pos)) return true;
  }
  return false;
}

bool Asteroid::kill() {
  if(thud_sound != NULL && invincible) {
    static Uint32 last_thud_tick = UINT32_MAX;
    Uint32 now = SDL_GetTicks();
    if(now - last_thud_tick >= 125) {
      last_thud_tick = now;
      Mix_PlayChannel(-1, thud_sound, 0);
    }
  }
  if(!invincible && !killed) {
    num_killable--;
    killed = true;
  }
  return CompositeObject::kill();
}

bool Asteroid::add_children(list<Asteroid*> *roids) {
  if(alive || children_added) return false;
  children_added = true;
  if(radius/2.0f < minimum_radius) {
    // explode good and proper
  } else {
    roids->push_back(new Asteroid(this));
    roids->push_back(new Asteroid(this));
  }
  if(explode_sound != NULL) {
    // Play at most once per millisecond tick: multiple asteroids dying in the
    // same frame would stack identical waveforms and clip the audio output.
    static Uint32 last_explode_tick = UINT32_MAX;
    Uint32 now = SDL_GetTicks();
    if(now != last_explode_tick) {
      last_explode_tick = now;
      Mix_PlayChannel(-1, explode_sound, 0);
    }
  }
  velocity = velocity / 8;
  return true;
}
