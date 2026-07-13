#include "shock.h"
#include "../asset_path.h"
#include "../ship.h"
#include "../point.h"
#include "../wrapped_point.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

// ── Bolt tuning ──────────────────────────────────────────────────────────────
const float ShockBolt::SEGMENT_LEN  = 55.0f;
const float ShockBolt::SEG_MS       = 9.0f;    // ~one segment per 8 ms tick
const int   ShockBolt::MAX_SEGMENTS = 14;      // reach ~ 14 * 55 = 770 units
const float ShockBolt::SEEK_RANGE   = 340.0f;  // how far a tip looks for a target
const float ShockBolt::HIT_RADIUS   = 16.0f;
const float ShockBolt::SPREAD       = 0.55f;   // radians of per-segment wobble
const float ShockBolt::STAGGER      = 18.0f;   // perpendicular offset for the zig-zag
const float ShockBolt::FADE_MS      = 150.0f;

static inline float frand() { return rand() / (float)RAND_MAX; }

ShockBolt::ShockBolt(WrappedPoint origin, Point facing_dir, Object *owner)
  : main_dir(facing_dir.normalized()),
    heading(facing_dir.normalized()),
    seg_accum(0.0f),
    growing(true),
    life(1.0f),
    owner(owner)
{
  points.reserve(MAX_SEGMENTS + 1);
  points.push_back(Point(origin.x(), origin.y()));
}

Object *ShockBolt::seek_target(const Point &tip, std::list<Object*> *lst,
                               bool skip_invincible, float &best_dist) const {
  Object *best = NULL;
  if (!lst) return best;
  for (Object *o : *lst) {
    if (o == owner) continue;  // a bolt never seeks the ship that fired it
    if (!o->alive) continue;
    if (skip_invincible && o->invincible) continue;
    bool skip = false;
    for (Object *a : avoid) if (a == o) { skip = true; break; }
    if (skip) continue;
    Point op = o->position.closest_to(tip);
    Point to_o = op - tip;
    // Only seek targets ahead of the tip within the front 180° of the firing
    // direction, so the arc never reaches backward past the ship.
    if (to_o.x() * main_dir.x() + to_o.y() * main_dir.y() <= 0.0f) continue;
    float d = to_o.magnitude() - o->radius;
    if (d < best_dist) { best_dist = d; best = o; }
  }
  return best;
}

void ShockBolt::grow_segment(std::list<Object*> *asteroids, std::list<Object*> *hostiles) {
  const Point tip = points.back();

  // Pick the single nearest target across asteroids and hostiles.
  float best = SEEK_RANGE;
  Object *target = seek_target(tip, asteroids, true, best);
  Object *h = seek_target(tip, hostiles, false, best);
  if (h) target = h;

  // Base direction: toward the target if we found one, else keep travelling.
  Point base = heading;
  if (target) base = (target->position.closest_to(tip) - tip).normalized();
  heading = base;

  // The drawn segment wobbles around the base direction and is nudged
  // sideways, giving the staggered forked-lightning look.
  Point seg_dir = base;
  seg_dir.rotate((frand() - 0.5f) * SPREAD);
  // Keep every segment within the front 180° of the firing direction: if the
  // jitter (or a boundary target) tipped it backward, clamp to the ±90° edge.
  if (seg_dir.x() * main_dir.x() + seg_dir.y() * main_dir.y() < 0.0f) {
    Point perp(-main_dir.y(), main_dir.x());
    float side = seg_dir.x() * perp.x() + seg_dir.y() * perp.y();
    seg_dir = (side >= 0.0f) ? perp : Point(-perp.x(), -perp.y());
  }
  Point next = tip + seg_dir * SEGMENT_LEN;
  next += seg_dir.perpendicular() * ((frand() - 0.5f) * STAGGER);
  points.push_back(next);

  // Did this segment reach the target? If so, snap onto it, record the hit and
  // let the next segment chain onward to whatever else is nearby.
  if (target) {
    Point tp = target->position.closest_to(next);
    if ((tp - next).magnitude() <= target->radius + HIT_RADIUS) {
      points.back() = tp;
      struck.push_back(target);
      avoid.push_back(target);
    }
  }
}

void ShockBolt::step_bolt(int delta, std::list<Object*> *asteroids, std::list<Object*> *hostiles) {
  seg_accum += delta;
  while (growing && (int)points.size() <= MAX_SEGMENTS && seg_accum >= SEG_MS) {
    seg_accum -= SEG_MS;
    grow_segment(asteroids, hostiles);
  }
  if ((int)points.size() > MAX_SEGMENTS) growing = false;
  if (!growing) life -= (float)delta / FADE_MS;
}

// ── Weapon ───────────────────────────────────────────────────────────────────

namespace Weapon {

static const int FIRE_INTERVAL = 200;  // ms between bolts while held

Shock::Shock(Ship *ship) : Base(ship), cooldown(0) {
  _name = "SHOCK";
  _ammo = 0;
  unlimited = false;

  shoot_sound = Mix_LoadWAV(asset_path("audio/shock.wav").c_str());
  if (shoot_sound == NULL) {
    // Fall back to the laser pew if the dedicated zap asset is missing.
    shoot_sound = Mix_LoadWAV(asset_path("audio/shoot.wav").c_str());
    if (shoot_sound == NULL)
      std::cout << "Unable to load shock.wav (" << Mix_GetError() << ")" << std::endl;
  }
  empty_sound = Mix_LoadWAV(asset_path("audio/empty.wav").c_str());
  if (empty_sound == NULL) {
    std::cout << "Unable to load empty.wav (" << Mix_GetError() << ")" << std::endl;
  }
}

Shock::~Shock() {
  if (shoot_sound) Mix_FreeChunk(shoot_sound);
  if (empty_sound) Mix_FreeChunk(empty_sound);
}

void Shock::shoot(bool on) {
  shooting = on;
  if (on) try_fire();  // fire the first bolt on the press, like the other weapons
}

void Shock::step(int delta) {
  cooldown -= delta;
  if (shooting) try_fire();  // keep arcing at FIRE_INTERVAL while held
}

void Shock::try_fire() {
  if (cooldown > 0) return;
  if (_ammo == 0) {
    if (empty_sound != NULL && ship->sound_volume_scale > 0.0f)
      Mix_PlayChannel(-1, empty_sound, 0);
    cooldown = FIRE_INTERVAL;  // don't spam the empty click every frame
    return;
  }
  _ammo--;
  ship->shocks.push_back(ShockBolt(ship->gun(), ship->facing.normalized(), ship));
  if (shoot_sound != NULL && ship->sound_volume_scale > 0.0f) {
    Mix_VolumeChunk(shoot_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
    Mix_PlayChannel(-1, shoot_sound, 0);
  }
  cooldown = FIRE_INTERVAL;
}

} // namespace Weapon
