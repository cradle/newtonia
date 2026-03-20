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
    radius = max_radius / 4 + rand() % (max_radius / 2 + 1);
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
    Mix_PlayChannel(-1, thud_sound, 0);
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
    Mix_PlayChannel(-1, explode_sound, 0);
  }
  velocity = velocity / 8;
  return true;
}
