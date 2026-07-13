#include "hazard.h"

#include <math.h>
#include <cstdlib>

#include "gl_compat.h"
#include "mat4.h"
#include "glship.h"
#include "ship.h"

using namespace std;

// ── Tuning ──────────────────────────────────────────────────────────────────
// PULSAR: charges quietly, then fires a shockwave that sweeps outward over
// EXPAND_MS to MAX_RADIUS. WAVE_BAND is comfortably larger than the per-step
// advance (~4 units) so a ship can never slip through the front between steps.
const float Hazard::PULSAR_CHARGE_MS  = 2400.0f;
const float Hazard::PULSAR_EXPAND_MS  = 1100.0f;
const float Hazard::PULSAR_MAX_RADIUS = 520.0f;
const float Hazard::PULSAR_CORE_RADIUS = 26.0f;
const float Hazard::WAVE_BAND         = 55.0f;
const float Hazard::KNOCKBACK         = 0.45f;

const float Hazard::COMET_SPEED  = 0.22f;   // well above the mini-station's drift
const float Hazard::SEEKER_SPEED = 0.135f;  // slow enough to out-fly
const int   Hazard::SEEKER_REWARD = 250;

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
      invincible = true;
      // Random starting phase so several pulsars don't fire in lockstep.
      timer_ = frand() * (PULSAR_CHARGE_MS + PULSAR_EXPAND_MS);
      break;
    case COMET:
      radius = 32.0f;
      velocity = Point(cosf(dir) * COMET_SPEED, sinf(dir) * COMET_SPEED);
      invincible = true;
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
  }
  radius_squared = radius * radius;
  alive = true;

  build_meshes();
}

bool Hazard::wave_active() const {
  if (kind_ != PULSAR) return false;
  float phase = fmodf(timer_, PULSAR_CHARGE_MS + PULSAR_EXPAND_MS);
  return phase >= PULSAR_CHARGE_MS;
}

bool Hazard::is_removable() const {
  // The pulsar and comet are permanent fixtures for the level; a destroyed
  // seeker lingers only until its debris burst has faded.
  if (kind_ != SEEKER) return false;
  return !alive && debris_.empty();
}

void Hazard::update(int delta, list<GLShip*> *players) {
  timer_ += delta;

  switch (kind_) {
    case PULSAR: {
      float cycle = PULSAR_CHARGE_MS + PULSAR_EXPAND_MS;
      if (timer_ >= cycle) timer_ -= cycle;  // keep the float bounded
      if (timer_ < PULSAR_CHARGE_MS) {
        wave_radius_ = 0.0f;
      } else {
        float t = (timer_ - PULSAR_CHARGE_MS) / PULSAR_EXPAND_MS;
        wave_radius_ = t * PULSAR_MAX_RADIUS;
      }
      break;
    }

    case COMET: {
      position += velocity * delta;
      position.wrap();
      rotation += rotation_speed * delta;  // tumble like an asteroid
      // Leave a trail of fading debris behind the head.
      trail_timer_ -= delta;
      if (trail_timer_ <= 0) {
        trail_timer_ += 45;
        Point jitter((frand() - 0.5f) * 0.03f, (frand() - 0.5f) * 0.03f);
        debris_.push_back(Particle(position, velocity * -0.15f + jitter, 550.0f));
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
          Point tgt = position.closest_to(nearest->ship->position);
          Point dir(tgt.x() - position.x(), tgt.y() - position.y());
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
  int count = 60;
  debris_.reserve(debris_.size() + count);
  for (int i = 0; i < count; i++) {
    float angle = frand() * 2.0f * (float)M_PI;
    float dist  = frand() * radius;
    Point start(position.x() + dist * cosf(angle),
                position.y() + dist * sinf(angle));
    float speed = 0.12f + frand() * 0.35f;
    Point vel(speed * cosf(angle), speed * sinf(angle));
    debris_.push_back(Particle(start, vel, 900.0f + frand() * 600.0f));
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
    if (kind_ == SEEKER && !alive) return;
    switch (kind_) {
      case PULSAR: map_mesh_.draw_tinted_at(0.7f, 0.5f, 1.0f, 1.0f, px, py, 0.0f); break;
      case COMET:  map_mesh_.draw_tinted_at(0.7f, 0.9f, 1.0f, 1.0f, px, py, 0.0f); break;
      case SEEKER: map_mesh_.draw_tinted_at(1.0f, 0.3f, 0.2f, 1.0f, px, py, 0.0f); break;
    }
    return;
  }

  if (kind_ == PULSAR) {
    float cycle = PULSAR_CHARGE_MS + PULSAR_EXPAND_MS;
    float phase = fmodf(timer_, cycle);
    // Charge ramps 0→1 through the charge window, then the core blazes and the
    // beams flare white for the brief emit window.
    bool firing = phase >= PULSAR_CHARGE_MS;
    float charge = firing ? 1.0f : phase / PULSAR_CHARGE_MS;
    float spin = timer_ * 0.09f;  // degrees; a steady lighthouse sweep

    // Beams: cyan while charging, flaring white-hot on emit. Faint so they read
    // as light, not solid geometry.
    float beam_a = (firing ? 0.85f : 0.20f + 0.35f * charge);
    beam_mesh_.draw_tinted_at(firing ? 1.0f : 0.5f,
                              firing ? 1.0f : 0.85f,
                              1.0f, beam_a, px, py, spin);

    // Core: dim blue at rest, white-hot when it fires.
    float core_r = 0.5f + 0.5f * charge;
    glLineWidth(2.0f);
    body_mesh_.draw_tinted_at(core_r, 0.6f + 0.4f * charge, 1.0f, 1.0f, px, py, 0.0f);

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
        mb.color(0.6f * al, 0.85f * al, 1.0f * al, al);
        mb.vertex(d.position.x(), d.position.y());
      }
      mb.end();
      mesh.upload(mb, GL_DYNAMIC_DRAW);
      mesh.draw(4.0f);
    }
    glLineWidth(2.0f);
    body_mesh_.draw_at(px, py, rotation);  // rotation is in degrees
    return;
  }

  // SEEKER
  if (alive) {
    glLineWidth(2.5f);
    body_mesh_.draw_at(px, py, 0.0f);
    // Blinking core light.
    float blink = 0.5f + 0.5f * sinf(timer_ * 0.012f);
    float m[16];
    mat4_identity(m);
    mat4_translate(m, m, px, py, 0.0f);
    mat4_scale(m, m, 0.28f, 0.28f, 1.0f);
    map_mesh_.draw_tinted_with_model(1.0f, 0.9f, 0.3f, blink, m);
  }
  if (!debris_.empty()) {
    static MeshBuilder mb;
    static Mesh mesh;
    mb.clear();
    mb.begin(GL_POINTS);
    for (const auto &d : debris_) {
      mb.color(1.0f, 0.5f, 0.2f, d.aliveness());
      mb.vertex(d.position.x(), d.position.y());
    }
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw(3.0f);
  }
}

// ── Serialisation ───────────────────────────────────────────────────────────

Save::Hazard Hazard::capture_state() const {
  Save::Hazard s;
  s.kind  = (uint8_t)kind_;
  s.pos_x = position.x();
  s.pos_y = position.y();
  s.vel_x = velocity.x();
  s.vel_y = velocity.y();
  s.timer = timer_;
  return s;
}

Hazard *Hazard::from_state(const Save::Hazard &s, const Point &world) {
  Hazard *h = new Hazard((Kind)s.kind, world);
  h->position = WrappedPoint(s.pos_x, s.pos_y);
  h->velocity = Point(s.vel_x, s.vel_y);
  h->timer_ = s.timer;
  return h;
}
