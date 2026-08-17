#include "shock.h"
#include "../asset_path.h"
#include "../ship.h"
#include "../asteroid.h"
#include "../grid.h"
#include "../seek.h"
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
const float ShockBolt::SPARK_MS     = 220.0f;  // spark lingers a touch past the line

static inline float frand() { return rand() / (float)RAND_MAX; }

// Squared distance from `center` to the segment [a,b]. Used so a hit is a
// segment-vs-body test, not an endpoint-only one: a fast 55-unit segment can
// cross a small asteroid and land past it, and an endpoint check would miss
// (the arc "passes through"). `center` must already be the target's nearest
// toroidal image (closest_to), so this is wrap-correct.
static float seg_point_dist2(const Point &a, const Point &b, const Point &center) {
  Point ab = b - a;
  float ab2 = ab.x() * ab.x() + ab.y() * ab.y();
  float t = ab2 > 1e-6f ? ((center.x() - a.x()) * ab.x() +
                           (center.y() - a.y()) * ab.y()) / ab2
                        : 0.0f;
  if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
  float dx = center.x() - (a.x() + ab.x() * t);
  float dy = center.y() - (a.y() + ab.y() * t);
  return dx * dx + dy * dy;
}

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
                               float &best_dist, bool asteroid_list) const {
  // The scan itself is the shared seek_nearest loop (seek.h); everything
  // in the filter below is this weapon's own law.
  return seek_nearest(tip, lst, best_dist,
      [this, asteroid_list](Object *o, const Point &to_o, float mag) {
        if (o == owner) return false;  // never the ship that fired it
        // Invincible ASTEROIDS are not sought: the arc would only dead-end
        // on a rock it can't destroy, wasting the bolt, so it ignores them
        // and chains to killable targets instead (they still BLOCK — see
        // grow_segment). The asteroid list is statically all-Asteroid (the
        // ship's missile list is the game's asteroid list), so no RTTI is
        // needed; the hostiles list never contains asteroids, so it skips
        // the check entirely. Invincible SHIPS (a shielded/god-mode player
        // under friendly fire) stay eligible — the arc paths to and stops
        // at them rather than arcing past.
        if (asteroid_list && static_cast<Asteroid *>(o)->invincible)
          return false;
        for (Object *a : avoid) if (a == o) return false;
        // Only seek targets inside the forward 135° cone (±67.5° of the
        // firing direction), so the arc reaches ahead and a little to the
        // side but never sharply back past the ship. main_dir is unit
        // length, so the dot product is |to_o|*cos(angle); keep it only
        // when cos(angle) >= cos(67.5°).
        static const float kCosHalfCone = 0.38268343f;  // cos(67.5°)
        return to_o.x() * main_dir.x() + to_o.y() * main_dir.y() >=
               mag * kCosHalfCone;
      });
}

void ShockBolt::grow_segment(std::list<Object*> *asteroids, std::list<Object*> *hostiles,
                             const Grid *grid) {
  const Point tip = points.back();

  // Pick the single nearest target across asteroids and hostiles.
  float best = SEEK_RANGE;
  Object *target = seek_target(tip, asteroids, best, /*asteroid_list=*/true);
  Object *h = seek_target(tip, hostiles, best, /*asteroid_list=*/false);
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

  // Invincible asteroids are never SOUGHT (seek_target skips them), but they
  // still BLOCK the arc, like the lance: if this segment runs into one, the
  // bolt stops at its surface instead of arcing through. Test the whole
  // segment, not just the endpoint, so a 55-unit step can't tunnel through a
  // small rock. Candidates come from the collision grid (segment query, like
  // the bullet sweep) rather than a full asteroid scan — CLAUDE.md convention
  // 6. Stop here directly (spark at the surface) — nothing to score, so no
  // struck entry — and the ended polyline replicates as-is.
  if (grid) {
    static std::vector<Object *> block_candidates;
    block_candidates.clear();
    grid->query_segment(tip, next, block_candidates);
    for (Object *o : block_candidates) {
      if (!o->alive) continue;
      Asteroid *ast = dynamic_cast<Asteroid *>(o);
      if (!ast || !ast->invincible) continue;
      Point c = o->position.closest_to(next);
      float reach = o->radius + HIT_RADIUS;
      if (seg_point_dist2(tip, next, c) <= reach * reach) {
        points.back() = surface_hit(o, c, tip);
        stop();  // absorbed by an invincible rock — arc ends at its surface
        return;
      }
    }
  }

  // Did this segment reach the target? Test the whole segment against the
  // target's body (not just the endpoint) so a fast step that crosses a small
  // rock still registers — an endpoint-only check let the arc pass through.
  // Snap onto the surface (not the centre, which would look pierced), record
  // the hit and let the next segment chain onward to whatever else is nearby.
  if (target) {
    Point c = target->position.closest_to(next);
    float reach = target->radius + HIT_RADIUS;
    if (seg_point_dist2(tip, next, c) <= reach * reach) {
      points.back() = surface_hit(target, c, tip);
      struck.push_back(target);
      avoid.push_back(target);
    }
  }
}

Point ShockBolt::surface_hit(Object *target, const Point &center, const Point &from) const {
  Point out = from - center;   // from the centre back toward the incoming bolt
  float m = out.magnitude();
  if (m < 1e-3f) return center;   // degenerate: bolt starts at the centre
  Point dir = out / m;
  Point stop = center + dir * target->radius;   // circle surface (ships/stations/hazards)
  // Asteroids are irregular polygons: walk outward from the centre along the
  // approach direction until we cross the real edge, matching the drawn outline.
  if (Asteroid *ast = dynamic_cast<Asteroid*>(target)) {
    float max_trace = ast->effective_radius() + 4.0f;
    for (float d = 1.0f; d <= max_trace; d += 1.0f) {
      if (!ast->contains(Point(ast->position.x() + dir.x() * d,
                               ast->position.y() + dir.y() * d))) {
        stop = center + dir * d;
        break;
      }
    }
  }
  return stop;
}

void ShockBolt::stop() {
  if (!growing) return;   // only the first stop ends the arc and sparks
  growing = false;
  if (points.empty()) return;
  // Spark burst at the collision point: a handful of short rays at random angles
  // and lengths, generated once so they hold steady while they fade.
  spark_pos = points.back();
  spark_life = 1.0f;
  spark_rays.clear();
  int n = 5 + rand() % 4;   // 5–8 rays
  for (int i = 0; i < n; i++) {
    float ang = frand() * 2.0f * (float)M_PI;
    float len = 10.0f + frand() * 18.0f;
    spark_rays.push_back(Point(cosf(ang) * len, sinf(ang) * len));
  }
}

void ShockBolt::step_bolt(int delta, std::list<Object*> *asteroids, std::list<Object*> *hostiles,
                          const Grid *grid) {
  seg_accum += delta;
  while (growing && (int)points.size() <= MAX_SEGMENTS && seg_accum >= SEG_MS) {
    seg_accum -= SEG_MS;
    grow_segment(asteroids, hostiles, grid);
  }
  if ((int)points.size() > MAX_SEGMENTS) growing = false;
  if (!growing) life -= (float)delta / FADE_MS;
  if (spark_life > 0.0f) spark_life -= (float)delta / SPARK_MS;
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
  shooting = on;  // armed on the trigger press; step() fires one bolt
}

void Shock::step(int delta) {
  cooldown -= delta;
  if (shooting && cooldown <= 0) {
    try_fire();
    cooldown = FIRE_INTERVAL;  // rate-limit rapid taps (empty click included)
  }
  // Semi-automatic, like Beam/Lance: one bolt per trigger pull, no
  // hold-to-repeat. Releasing and pressing fire again shoots the next.
  shooting = false;
}

void Shock::try_fire() {
  // Netplay: like Default/Beam/Lance, the host's remote-player shock keeps its
  // ammo + cooldown bookkeeping but mints no bolt and plays no sound — the
  // real polyline arrives as MSG_SHOCK. The ammo decrement below MUST still
  // run on the host: it is what keeps the host's snapshot ammo in step with
  // the client's firing. Returning early here (skipping the decrement) left
  // the host's count pinned, so the 10 Hz snapshot restore reset the client's
  // ammo back up after every shot.
  bool sim_only = ship->net_remote_gun;
  if (_ammo == 0) {
    if (empty_sound != NULL && !sim_only && ship->sound_volume_scale > 0.0f)
      Mix_PlayChannel(-1, empty_sound, 0);
    return;
  }
  _ammo--;
  if (sim_only) return;
  ship->shocks.push_back(ShockBolt(ship->gun(), ship->facing.normalized(), ship));
  if (shoot_sound != NULL && ship->sound_volume_scale > 0.0f) {
    Mix_VolumeChunk(shoot_sound, (int)(MIX_MAX_VOLUME * ship->sound_volume_scale));
    Mix_PlayChannel(-1, shoot_sound, 0);
  }
}

} // namespace Weapon
