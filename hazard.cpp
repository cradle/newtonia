#include "hazard.h"

#include <math.h>
#include <cstdlib>

#include "gl_compat.h"
#include "mat4.h"
#include "glship.h"
#include "ship.h"
#include "seek.h"

using namespace std;

// ── Tuning ──────────────────────────────────────────────────────────────────
// PULSAR: charges quietly, then fires a shockwave that sweeps outward over
// EXPAND_MS to MAX_RADIUS. WAVE_BAND is comfortably larger than the per-step
// advance (~4 units) so a ship can never slip through the front between steps.
const float Hazard::PULSAR_CHARGE_MS  = 2400.0f;
const float Hazard::PULSAR_EXPAND_MS  = 1100.0f;
const float Hazard::PULSAR_MAX_RADIUS = 520.0f;
const float Hazard::PULSAR_CORE_RADIUS = 26.0f;
const int   Hazard::PULSAR_HEALTH     = 6;      // tanky — shoot it between waves
const float Hazard::WAVE_BAND         = 55.0f;
// Outward acceleration (per ms) applied while a ship sits inside the wavefront
// band — not an instant impulse, so a full pass firmly shoves without a spike.
const float Hazard::KNOCKBACK         = 0.0014f;

const float Hazard::COMET_SPEED  = 0.28f;   // fast — a real dodge, not a drift
const int   Hazard::COMET_HEALTH = 4;       // shots to break it up
const float Hazard::SEEKER_SPEED = 0.135f;  // slow enough to out-fly
// Faster than the seeker but capped well under the player's plain thrust
// (0.2) like the interceptor's ~0.85x rule — a committed straight-line
// escape always wins; the intercept steering is the whole upgrade.
const float Hazard::HUNTER_SPEED  = 0.155f;
const int   Hazard::HUNTER_HEALTH = 3;
const int   Hazard::SEEKER_REWARD = 250;
const int   Hazard::COMET_REWARD  = 500;
const int   Hazard::PULSAR_REWARD = 750;
const int   Hazard::HUNTER_REWARD = 600;

static float frand() { return (float)(rand() % 100000) / 100000.0f; }

Hazard::Hazard(Kind kind, const Point &world) : kind_(kind) {
  // Pick a spawn clear of the world centre (black hole + player start).
  float cx = world.x() * 0.5f, cy = world.y() * 0.5f;
  float px = cx, py = cy;
  for (int tries = 0; tries < 24; tries++) {
    px = frand() * world.x();
    py = frand() * world.y();
    float dx = px - cx, dy = py - cy;
    if (sqrtf(dx * dx + dy * dy) > world.x() * 0.22f) break;
  }
  position = WrappedPoint(px, py);

  float dir = frand() * 2.0f * (float)M_PI;
  switch (kind_) {
    case PULSAR:
      radius = PULSAR_CORE_RADIUS;
      velocity = Point(0.0f, 0.0f);
      invincible = false;   // shots break the core
      health_ = PULSAR_HEALTH;
      // Random starting phase so several pulsars don't fire in lockstep.
      timer_ = frand() * (PULSAR_CHARGE_MS + PULSAR_EXPAND_MS);
      break;
    case COMET:
      radius = 32.0f;
      velocity = Point(cosf(dir) * COMET_SPEED, sinf(dir) * COMET_SPEED);
      invincible = false;   // shots break it up
      health_ = COMET_HEALTH;
      // Tumble like an asteroid (degrees/ms, same formula as Asteroid).
      rotation_speed = ((rand() % 15) - 7) / radius;
      if (rotation_speed > -0.08f && rotation_speed < 0.08f)
        rotation_speed = (rand() % 2) ? 0.12f : -0.12f;
      break;
    case SEEKER:
      radius = 20.0f;
      velocity = Point(cosf(dir) * SEEKER_SPEED, sinf(dir) * SEEKER_SPEED);
      invincible = false;  // a single shot destroys it
      break;
    case HUNTER:
      radius = 30.0f;
      velocity = Point(cosf(dir) * HUNTER_SPEED, sinf(dir) * HUNTER_SPEED);
      invincible = false;
      health_ = HUNTER_HEALTH;
      break;
  }
  radius_squared = radius * radius;
  alive = true;

  build_meshes();
}

bool Hazard::wave_active() const {
  if (kind_ != PULSAR || !alive) return false;  // a dead pulsar fires nothing
  float phase = fmodf(timer_, PULSAR_CHARGE_MS + PULSAR_EXPAND_MS);
  return phase >= PULSAR_CHARGE_MS;
}

bool Hazard::is_removable() const {
  // A destroyed hazard lingers only until its debris burst has faded, then it
  // is reaped. Live hazards are never removable.
  return !alive && debris_.empty();
}

void Hazard::hit() {
  // The seeker dies in one shot via destroy(); the comet, pulsar and hunter
  // have a health pool to chip down here.
  if (kind_ == SEEKER || !alive) return;
  // Chip off a puff of impact debris on every hit.
  for (int i = 0; i < 10; i++) {
    float a = frand() * 2.0f * (float)M_PI;
    float sp = 0.05f + frand() * 0.2f;
    debris_.push_back(Particle(position, Point(cosf(a) * sp, sinf(a) * sp),
                               400.0f + frand() * 300.0f));
  }
  if (--health_ <= 0) destroy();
}

void Hazard::update(int delta, list<GLShip*> *players) {
  timer_ += delta;

  switch (kind_) {
    case PULSAR: {
      if (!alive) break;  // destroyed: only its debris keeps evolving
      float cycle = PULSAR_CHARGE_MS + PULSAR_EXPAND_MS;
      if (timer_ >= cycle) timer_ -= cycle;  // keep the float bounded
      if (timer_ < PULSAR_CHARGE_MS) {
        wave_radius_ = 0.0f;
      } else {
        float t = (timer_ - PULSAR_CHARGE_MS) / PULSAR_EXPAND_MS;
        wave_radius_ = t * PULSAR_MAX_RADIUS;
      }
      // Beams spin off their own accumulator (not the cycle timer, which resets
      // and would snap the beams back each loop). Wrapped at 360 so it never
      // loses precision, seamlessly.
      rotation += 0.09f * delta;
      if (rotation >= 360.0f) rotation -= 360.0f;
      break;
    }

    case COMET: {
      if (!alive) break;  // destroyed: stop moving and stop trailing, just fade
      position += velocity * delta;
      position.wrap();
      rotation += rotation_speed * delta;  // tumble like an asteroid
      // Lay down a bright, lengthening tail: debris lag behind the head so the
      // fast comet stretches them into a visible streak.
      trail_timer_ -= delta;
      if (trail_timer_ <= 0) {
        trail_timer_ += 28;
        for (int i = 0; i < 2; i++) {
          Point jitter((frand() - 0.5f) * 0.05f, (frand() - 0.5f) * 0.05f);
          debris_.push_back(Particle(position, velocity * -0.25f + jitter, 750.0f));
        }
      }
      break;
    }

    case SEEKER: {
      if (alive) {
        // Steer toward the nearest living player; a gentle turn rate keeps it
        // dodgeable rather than a guaranteed hit.
        GLShip *nearest = NULL;
        float best = 0.0f;
        if (players != NULL) {
          for (auto *gs : *players) {
            if (!gs->ship->is_alive()) continue;
            float d = position.distance_to(gs->ship->position);
            if (nearest == NULL || d < best) { best = d; nearest = gs; }
          }
        }
        if (nearest != NULL) {
          // closest_to() returns the wrapped copy of *our* position nearest the
          // target; steer from that copy toward the target's actual position
          // (mirrors GLMiniStation::fire_at_nearest_player).
          Point self = position.closest_to(nearest->ship->position);
          Point dir(nearest->ship->position.x() - self.x(),
                    nearest->ship->position.y() - self.y());
          float m = dir.magnitude();
          if (m > 1e-4f) {
            Point desired = (dir / m) * SEEKER_SPEED;
            float turn = (float)delta * 0.004f;
            if (turn > 1.0f) turn = 1.0f;
            velocity += (desired - velocity) * turn;
          }
        }
        position += velocity * delta;
        position.wrap();
        rotation += 0.04f * delta;  // slow idle spin (deg/ms, ~9s per turn)
        if (rotation >= 360.0f) rotation -= 360.0f;
      }
      break;
    }

    case HUNTER: {
      if (alive) {
        GLShip *nearest = NULL;
        float best = 0.0f;
        if (players != NULL) {
          for (auto *gs : *players) {
            if (!gs->ship->is_alive()) continue;
            float d = position.distance_to(gs->ship->position);
            if (nearest == NULL || d < best) { best = d; nearest = gs; }
          }
        }
        if (nearest != NULL) {
          Point self = position.closest_to(nearest->ship->position);
          Point rel(nearest->ship->position.x() - self.x(),
                    nearest->ship->position.y() - self.y());
          // Steer at the INTERCEPT point, not the target: the shared
          // seek.h solver at full lead, with the hunter itself as the
          // "bullet". The horizon caps the prediction so a target it
          // cannot cut off (fleeing faster, or too far) degrades to the
          // seeker's plain chase instead of aiming at infinity.
          Point offset = intercept_offset(rel, nearest->ship->velocity,
                                          HUNTER_SPEED, 6000.0f, 1.0f);
          Point aim(rel.x() + offset.x(), rel.y() + offset.y());
          float m = aim.magnitude();
          if (m > 1e-4f) {
            Point desired = (aim / m) * HUNTER_SPEED;
            float turn = (float)delta * 0.004f;
            if (turn > 1.0f) turn = 1.0f;
            velocity += (desired - velocity) * turn;
          }
        }
        position += velocity * delta;
        position.wrap();
        rotation += 0.07f * delta;  // quicker spin than the seeker: menace
        if (rotation >= 360.0f) rotation -= 360.0f;
      }
      break;
    }
  }

  // Advance and cull any active debris (comet trail / seeker death burst).
  for (size_t i = 0; i < debris_.size();) {
    debris_[i].step(delta);
    if (!debris_[i].is_alive()) {
      debris_[i] = std::move(debris_.back());
      debris_.pop_back();
    } else {
      ++i;
    }
  }
}

void Hazard::destroy() {
  if (!alive) return;
  alive = false;
  // Drop any lingering trail/impact debris so the death reads as one clean
  // radial burst rather than a streak that keeps drifting off.
  debris_.clear();
  // 90 slow particles: the old 60 at up to 0.47 u/ms dispersed into
  // star-noise confetti within ~300 ms — at normal viewing the seeker's
  // death read as "no explosion about half the time" (field, 2026-08-23,
  // confirmed frame-stepping the reporter's replay). A death burst has to
  // stay a coherent fireball for most of its life.
  int count = 90;
  debris_.reserve(debris_.size() + count);
  for (int i = 0; i < count; i++) {
    float angle = frand() * 2.0f * (float)M_PI;
    float dist  = frand() * radius;
    Point start(position.x() + dist * cosf(angle),
                position.y() + dist * sinf(angle));
    float speed = 0.05f + frand() * 0.16f;
    Point vel(speed * cosf(angle), speed * sinf(angle));
    debris_.push_back(Particle(start, vel, 1100.0f + frand() * 700.0f));
  }
}

// ── Mesh construction ───────────────────────────────────────────────────────

void Hazard::build_meshes() {
  const int seg = 24;

  // Minimap blip: a small filled octagon, tinted per-kind at draw time.
  {
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 8; i++) {
      float a = i * 2.0f * (float)M_PI / 8;
      mb.vertex(cosf(a) * radius, sinf(a) * radius);
    }
    mb.end();
    map_mesh_.upload(mb);
  }

  if (kind_ == PULSAR) {
    // A neutron-star look: a small blazing core, a faint accretion ring, and
    // two lighthouse beams that sweep round (built separately so they can be
    // rotated each frame).
    MeshBuilder mb;
    // Hot core.
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(1.0f, 1.0f, 1.0f);
    mb.vertex(0.0f, 0.0f);
    for (int i = 0; i <= seg; i++) {
      float a = i * 2.0f * (float)M_PI / seg;
      mb.vertex(cosf(a) * radius * 0.42f, sinf(a) * radius * 0.42f);
    }
    mb.end();
    // Accretion ring.
    mb.begin(GL_LINE_LOOP);
    mb.color(0.55f, 0.75f, 1.0f);
    for (int i = 0; i < seg; i++) {
      float a = i * 2.0f * (float)M_PI / seg;
      mb.vertex(cosf(a) * radius, sinf(a) * radius);
    }
    mb.end();
    body_mesh_.upload(mb);

    // Two opposing tapered beams along ±x (rotated at draw time).
    MeshBuilder bm;
    float w = radius * 0.22f, len = radius * 3.0f;
    bm.begin(GL_TRIANGLES);
    bm.color(1.0f, 1.0f, 1.0f);
    bm.vertex(0.0f,  w); bm.vertex(0.0f, -w); bm.vertex( len, 0.0f);
    bm.vertex(0.0f,  w); bm.vertex(0.0f, -w); bm.vertex(-len, 0.0f);
    bm.end();
    beam_mesh_.upload(bm);

    // Unit circle (radius 1) scaled to the shockwave radius each frame.
    MeshBuilder rb;
    rb.begin(GL_LINE_LOOP);
    rb.color(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 48; i++) {
      float a = i * 2.0f * (float)M_PI / 48;
      rb.vertex(cosf(a), sinf(a));
    }
    rb.end();
    ring_mesh_.upload(rb);
  } else if (kind_ == COMET) {
    // A solid-white, irregular asteroid-shaped body (9 vertices with per-vertex
    // radius offsets, exactly like Asteroid) that spins as it flies.
    float off[9];
    for (int i = 0; i < 9; i++) off[i] = 0.7f + (rand() / (float)RAND_MAX) * 0.6f;
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(1.0f, 1.0f, 1.0f);
    mb.vertex(0.0f, 0.0f);
    for (int i = 0; i <= 9; i++) {
      int k = i % 9;
      float a = k * 2.0f * (float)M_PI / 9;
      mb.vertex(cosf(a) * radius * off[k], sinf(a) * radius * off[k]);
    }
    mb.end();
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 9; i++) {
      float a = i * 2.0f * (float)M_PI / 9;
      mb.vertex(cosf(a) * radius * off[i], sinf(a) * radius * off[i]);
    }
    mb.end();
    body_mesh_.upload(mb);
  } else if (kind_ == HUNTER) {
    // The seeker's elite: a bigger triple diamond in deeper crimson, with
    // barbs off the four points — must read apart from the seeker at a
    // glance (late-game design rule: readability).
    MeshBuilder mb;
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 0.15f, 0.1f);
    mb.vertex(0.0f, radius);
    mb.vertex(radius, 0.0f);
    mb.vertex(0.0f, -radius);
    mb.vertex(-radius, 0.0f);
    mb.end();
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 0.3f, 0.15f);
    mb.vertex(0.0f, radius * 0.66f);
    mb.vertex(radius * 0.66f, 0.0f);
    mb.vertex(0.0f, -radius * 0.66f);
    mb.vertex(-radius * 0.66f, 0.0f);
    mb.end();
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 0.5f, 0.25f);
    mb.vertex(0.0f, radius * 0.33f);
    mb.vertex(radius * 0.33f, 0.0f);
    mb.vertex(0.0f, -radius * 0.33f);
    mb.vertex(-radius * 0.33f, 0.0f);
    mb.end();
    // Barbs: short spikes past each point of the outer diamond.
    mb.begin(GL_LINES);
    mb.color(1.0f, 0.15f, 0.1f);
    mb.vertex(0.0f, radius);  mb.vertex(0.0f, radius * 1.35f);
    mb.vertex(radius, 0.0f);  mb.vertex(radius * 1.35f, 0.0f);
    mb.vertex(0.0f, -radius); mb.vertex(0.0f, -radius * 1.35f);
    mb.vertex(-radius, 0.0f); mb.vertex(-radius * 1.35f, 0.0f);
    mb.end();
    body_mesh_.upload(mb);
  } else {  // SEEKER
    // Hostile-red drone: a diamond outline around a small core.
    MeshBuilder mb;
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 0.3f, 0.2f);
    mb.vertex(0.0f, radius);
    mb.vertex(radius, 0.0f);
    mb.vertex(0.0f, -radius);
    mb.vertex(-radius, 0.0f);
    mb.end();
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 0.5f, 0.3f);
    mb.vertex(0.0f, radius * 0.5f);
    mb.vertex(radius * 0.5f, 0.0f);
    mb.vertex(0.0f, -radius * 0.5f);
    mb.vertex(-radius * 0.5f, 0.0f);
    mb.end();
    body_mesh_.upload(mb);
  }
}

// ── Drawing ─────────────────────────────────────────────────────────────────

void Hazard::draw(bool minimap) const {
  float px = position.x(), py = position.y();

  if (minimap) {
    if ((kind_ == SEEKER || kind_ == HUNTER) && !alive) return;
    switch (kind_) {
      case PULSAR: map_mesh_.draw_tinted_at(0.7f, 0.5f, 1.0f, 1.0f, px, py, 0.0f); break;
      case COMET:  map_mesh_.draw_tinted_at(0.7f, 0.9f, 1.0f, 1.0f, px, py, 0.0f); break;
      case SEEKER: map_mesh_.draw_tinted_at(1.0f, 0.3f, 0.2f, 1.0f, px, py, 0.0f); break;
      case HUNTER: map_mesh_.draw_tinted_at(1.0f, 0.1f, 0.05f, 1.0f, px, py, 0.0f); break;
    }
    return;
  }

  if (kind_ == PULSAR) {
    // Impact/break-up debris (from hits, and the final burst) always render.
    if (!debris_.empty()) {
      static MeshBuilder mb;
      static Mesh mesh;
      mb.clear();
      mb.begin(GL_POINTS);
      for (const auto &d : debris_) {
        float al = d.aliveness();
        mb.color(0.7f, 0.75f + 0.25f * al, 1.0f, al);
        mb.vertex(d.position.x(), d.position.y());
      }
      mb.end();
      mesh.upload(mb, GL_DYNAMIC_DRAW);
      mesh.draw(5.0f);
    }
    if (!alive) return;  // destroyed: nothing left but the fading debris

    float cycle = PULSAR_CHARGE_MS + PULSAR_EXPAND_MS;
    float phase = fmodf(timer_, cycle);
    // Energy ramps 0→1 while charging, then 1→0 as it discharges through the
    // emit window. It is exactly 0 at both ends of the cycle, so brightness has
    // no seam at the loop point.
    float e = phase < PULSAR_CHARGE_MS
                ? phase / PULSAR_CHARGE_MS
                : 1.0f - (phase - PULSAR_CHARGE_MS) / PULSAR_EXPAND_MS;

    // Beams: faint cyan when idle, flaring toward white as energy peaks. Kept
    // translucent so they read as light rather than solid geometry.
    beam_mesh_.draw_tinted_at(0.5f + 0.5f * e, 0.7f + 0.3f * e, 1.0f,
                              0.20f + 0.65f * e, px, py, rotation);

    // Core: dim blue at rest, white-hot at the peak.
    glLineWidth(2.0f);
    body_mesh_.draw_tinted_at(0.5f + 0.5f * e, 0.6f + 0.4f * e, 1.0f, 1.0f, px, py, 0.0f);

    if (wave_radius_ > 1.0f) {
      float t = wave_radius_ / PULSAR_MAX_RADIUS;
      float a = 1.0f - t;  // fade as it expands
      // A bright leading ring with a fainter trailing one, shifting amber→red.
      float model[16];
      mat4_identity(model);
      mat4_translate(model, model, px, py, 0.0f);
      mat4_scale(model, model, wave_radius_, wave_radius_, 1.0f);
      glLineWidth(3.5f);
      ring_mesh_.draw_tinted_with_model(1.0f, 0.7f - 0.5f * t, 0.15f, a, model);
      if (wave_radius_ > 24.0f) {
        float inner = wave_radius_ - 18.0f;
        float m2[16];
        mat4_identity(m2);
        mat4_translate(m2, m2, px, py, 0.0f);
        mat4_scale(m2, m2, inner, inner, 1.0f);
        glLineWidth(2.0f);
        ring_mesh_.draw_tinted_with_model(1.0f, 0.5f - 0.4f * t, 0.1f, a * 0.5f, m2);
      }
    }
    return;
  }

  if (kind_ == COMET) {
    if (!debris_.empty()) {
      static MeshBuilder mb;
      static Mesh mesh;
      mb.clear();
      mb.begin(GL_POINTS);
      for (const auto &d : debris_) {
        float al = d.aliveness();
        // Icy white core fading to blue at the tail's end.
        mb.color(0.75f + 0.25f * al, 0.85f + 0.15f * al, 1.0f, al);
        mb.vertex(d.position.x(), d.position.y());
      }
      mb.end();
      mesh.upload(mb, GL_DYNAMIC_DRAW);
      mesh.draw(6.0f);
    }
    if (alive) {
      glLineWidth(2.0f);
      body_mesh_.draw_at(px, py, rotation);  // rotation is in degrees
    }
    return;
  }

  // SEEKER / HUNTER (the hunter shares the drone draw: its own bigger
  // crimson mesh, and a blink that quickens with damage taken — the
  // health readout, like the tough asteroids' cracks).
  if (alive) {
    glLineWidth(2.5f);
    body_mesh_.draw_at(px, py, rotation);  // slow idle spin
    // Blinking core light.
    float blink_rate = kind_ == HUNTER
        ? 0.012f * (float)(1 + (HUNTER_HEALTH - health_))
        : 0.012f;
    float blink = 0.5f + 0.5f * sinf(timer_ * blink_rate);
    float m[16];
    mat4_identity(m);
    mat4_translate(m, m, px, py, 0.0f);
    mat4_scale(m, m, 0.28f, 0.28f, 1.0f);
    map_mesh_.draw_tinted_with_model(1.0f, 0.9f, 0.3f, blink, m);
  }
  if (!debris_.empty()) {
    // TRIANGLE quads, not GL_POINTS: the reporter's GPU rendered this
    // block's point-sprite burst as nothing at all (same build and replay
    // drew fine under llvmpipe — a driver-side point-size lottery the
    // asteroid/ship debris happens to win at size 3 and this burst lost
    // at 6). Triangles rasterize identically everywhere, and world-unit
    // sizing keeps the burst's weight independent of window resolution —
    // the same class of fix as the line emulation's devicePixelRatio
    // scaling (gles2_compat).
    static MeshBuilder mb;
    static Mesh mesh;
    mb.clear();
    mb.begin(GL_TRIANGLES);
    for (const auto &d : debris_) {
      // Hold full brightness for the first ~40% of a particle's life, then
      // fade — a linear fade dropped the whole burst under the starfield's
      // noise floor almost immediately. Hotter (whiter) while bright.
      float al = d.aliveness();
      float a = al > 0.6f ? 1.0f : al / 0.6f;
      float x = d.position.x(), y = d.position.y();
      // Each ember is a tiny diamond, near point-sized (a couple of world
      // units — 2-3 px at typical zoom), faintly streaked along its own
      // velocity so it still reads as a spark, shrinking as it cools.
      // Big flat quads read as blocky confetti (field, 2026-08-23).
      float vx = d.velocity.x(), vy = d.velocity.y();
      float vm = sqrtf(vx * vx + vy * vy);
      float dx = vm > 1e-5f ? vx / vm : 1.0f;
      float dy = vm > 1e-5f ? vy / vm : 0.0f;
      float len = 2.8f * (0.5f + 0.5f * a);
      float wid = 1.8f * (0.5f + 0.5f * a);
      float px_ = -dy * wid, py_ = dx * wid;
      float hx = dx * len, hy = dy * len;
      mb.color(1.0f, 0.45f + 0.45f * a, 0.2f, a);
      mb.vertex(x + hx, y + hy); mb.vertex(x + px_, y + py_); mb.vertex(x - hx, y - hy);
      mb.vertex(x + hx, y + hy); mb.vertex(x - hx, y - hy); mb.vertex(x - px_, y - py_);
    }
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }
}

// ── Serialisation ───────────────────────────────────────────────────────────

Save::Hazard Hazard::capture_state() const {
  Save::Hazard s;
  s.kind   = (uint8_t)kind_;
  s.pos_x  = position.x();
  s.pos_y  = position.y();
  s.vel_x  = velocity.x();
  s.vel_y  = velocity.y();
  s.timer  = timer_;
  s.health = health_;
  return s;
}

Hazard *Hazard::from_state(const Save::Hazard &s, const Point &world) {
  // A kind beyond the newest (doctored save, hostile snapshot) would build
  // an alive hazard no draw or collision case can touch — invisible,
  // unkillable, and permanently blocking the level-clear gate. Refuse it;
  // both callers skip a NULL.
  if (s.kind > (uint8_t)HUNTER) return NULL;
  Hazard *h = new Hazard((Kind)s.kind, world);
  h->position = WrappedPoint(s.pos_x, s.pos_y);
  h->velocity = Point(s.vel_x, s.vel_y);
  h->timer_ = s.timer;
  h->health_ = s.health;
  return h;
}

void Hazard::apply_net_state(const Save::Hazard &s) {
  // In-place reconcile for the net client: snap pose/velocity and re-sync the
  // cycle timer + health to the host. Meshes, debris and the comet trail are
  // left untouched so replicas keep their continuity between snapshots; the
  // client extrapolates motion with update() in the gaps.
  position = WrappedPoint(s.pos_x, s.pos_y);
  velocity = Point(s.vel_x, s.vel_y);
  timer_   = s.timer;
  health_  = s.health;
}
