#include "gl_mini_station.h"
#include <math.h>
#include <cstdlib>

#include "gl_compat.h"
#include "mesh.h"
#include "mat4.h"
#include "particle.h"
#include "ship.h"
#include "grid.h"
#include "asset_path.h"
#include <iostream>

using namespace std;

const float GLMiniStation::SHOOT_INTERVAL = 3000.0f;  // fire every 3 seconds
const float GLMiniStation::DRIFT_SPEED    = 0.12f;    // steady cruise speed

GLMiniStation::GLMiniStation(const Grid &grid, list<GLShip*>* players, list<Object*>* /*asteroids*/)
    : Ship(grid, false), players(players) {
  // Smaller than the wave-deploying GLStation (radius 200).
  radius = 70.0f;
  radius_squared = radius * radius;

  // Drift in a single, randomly chosen direction at a steady speed. With
  // friction disabled (Ship(grid, false)) this velocity persists forever.
  float dir = (float)(rand() % 100000) / 100000.0f * 2.0f * (float)M_PI;
  velocity = Point(cosf(dir) * DRIFT_SPEED, sinf(dir) * DRIFT_SPEED);
  facing = Point(cosf(dir), sinf(dir));

  lives = 1;
  alive = true;
  time_until_respawn = 0;
  time_until_next_shot = SHOOT_INTERVAL;

  // The station drifts but never thrusts, so silence the idle engine hum that
  // Ship's constructor starts on a looping channel.
  mute_engine();

  outer_rotation_speed = 0.01f;
  inner_rotation_speed = -0.0025f;
  inner_rotation = outer_rotation = 0.0f;

  shoot_sound = Mix_LoadWAV(asset_path("audio/shoot.wav").c_str());
  if (shoot_sound == NULL) {
    std::cout << "Unable to load shoot.wav (" << Mix_GetError() << ")" << std::endl;
  }

  float r = radius, r2 = radius * 0.9f;
  float segment_size = 360.0f / NUM_SEGMENTS;

  {
    MeshBuilder mb;
    // Black filled disc (centre vertex first for proper fan)
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex(0.0f, 0.0f);
    for (float i = 0.0f; i <= 360.0f; i += segment_size) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r*cosf(d), r*sinf(d));
    }
    mb.end();
    // Outer ring circle
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 1.0f, 1.0f);
    for (float i = 0.0f; i < 360.0f; i += segment_size) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r*cosf(d), r*sinf(d));
    }
    mb.end();
    // Inner ring circle
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 1.0f, 1.0f);
    for (float i = 0.0f; i < 360.0f; i += segment_size) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r2*cosf(d), r2*sinf(d));
    }
    mb.end();
    // Radial divider spokes
    mb.begin(GL_LINES);
    mb.color(1.0f, 1.0f, 1.0f);
    for (float i = 0.0f; i < 360.0f; i += segment_size) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r *cosf(d), r *sinf(d));
      mb.vertex(r2*cosf(d), r2*sinf(d));
    }
    mb.end();
    body_mesh.upload(mb);
  }

  {
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(1.0f, 1.0f, 1.0f);
    float mseg = 360.0f / 8;
    for (float i = 0.0f; i < 360.0f; i += mseg) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r*cosf(d), r*sinf(d));
    }
    mb.end();
    map_body_mesh.upload(mb);
  }
}

GLMiniStation::~GLMiniStation() {
  if (shoot_sound != NULL)
    Mix_FreeChunk(shoot_sound);
}

void GLMiniStation::destroy() {
  if (!alive) return;
  alive = false;
  lives = 0;
  // The destruction sound is owned and played by GLGame (so the chunk safely
  // outlives this object, which is deleted once its debris has faded).
  // Radial debris burst sized to the station's radius.
  int count = 120;
  debris.reserve(debris.size() + count);
  for (int i = 0; i < count; i++) {
    float angle = (float)(rand() % 100000) / 100000.0f * 2.0f * (float)M_PI;
    float dist  = (float)(rand() % (int)radius);
    Point start(position.x() + dist * cosf(angle),
                position.y() + dist * sinf(angle));
    float speed = 0.15f + (float)(rand() % 100) / 222.0f;
    Point vel(speed * cosf(angle), speed * sinf(angle));
    debris.push_back(Particle(start, vel, 1500.0f + rand() % 1000));
  }
}

Save::MiniStation GLMiniStation::capture_state() const {
  Save::MiniStation s;
  s.present = true;
  s.alive = alive;
  s.pos_x = position.x();
  s.pos_y = position.y();
  s.vel_x = velocity.x();
  s.vel_y = velocity.y();
  s.inner_rotation = inner_rotation;
  s.outer_rotation = outer_rotation;
  s.time_until_next_shot = time_until_next_shot;
  return s;
}

void GLMiniStation::restore_state(const Save::MiniStation &s) {
  alive = s.alive;
  lives = s.alive ? 1 : 0;
  position = WrappedPoint(s.pos_x, s.pos_y);
  velocity = Point(s.vel_x, s.vel_y);
  // Keep the muzzle aimed along the direction of travel until the next shot
  // recomputes it; harmless if velocity is (near) zero.
  if (velocity.magnitude() > 1e-6f)
    facing = velocity.normalized();
  inner_rotation = s.inner_rotation;
  outer_rotation = s.outer_rotation;
  time_until_next_shot = s.time_until_next_shot;
}

void GLMiniStation::fire_at_nearest_player() {
  if (players == NULL) return;

  // Find the nearest living player using toroidal (wrapped) distance.
  GLShip *nearest = NULL;
  float best = 0.0f;
  for (auto *gs : *players) {
    if (!gs->ship->is_alive()) continue;
    float d = position.distance_to(gs->ship->position);
    if (nearest == NULL || d < best) {
      best = d;
      nearest = gs;
    }
  }
  if (nearest == NULL) return;

  // Aim across the nearest world wrap so we shoot the short way round.
  Point self = position.closest_to(nearest->ship->position);
  Point dir(nearest->ship->position.x() - self.x(),
            nearest->ship->position.y() - self.y());
  float m = dir.magnitude();
  if (m < 1e-4f) return;
  dir = dir / m;
  facing = dir;

  // Spawn just outside the ring; bullet kinematics match a player shot.
  WrappedPoint muzzle(position.x() + dir.x() * (radius + 5.0f),
                      position.y() + dir.y() * (radius + 5.0f));
  bullets.push_back(Particle(muzzle, dir * 0.615f + velocity * 0.99f, 2000.0f));

  if (shoot_sound != NULL)
    Mix_PlayChannel(-1, shoot_sound, 0);
}

void GLMiniStation::step(float delta, const Grid &grid) {
  Ship::step(delta, grid);

  outer_rotation += outer_rotation_speed * delta;
  inner_rotation += inner_rotation_speed * delta;

  if (alive) {
    time_until_next_shot -= delta;
    if (time_until_next_shot <= 0.0f) {
      time_until_next_shot += SHOOT_INTERVAL;
      fire_at_nearest_player();
    }
  }

  // The station passes straight through asteroids — only its bullets interact,
  // exactly the way player shots do (but the score they earn goes nowhere).
  collide_bullets_with_asteroids(grid, (int)delta);
}

void GLMiniStation::draw_bullets() const {
  if (bullets.empty()) return;
  static MeshBuilder mb;
  static Mesh mesh;
  mb.clear();
  mb.begin(GL_LINES);
  for (const auto &b : bullets) {
    mb.color(1.0f, 0.3f, 0.2f);   // hostile bullets render red
    Point tail = b.position - b.velocity * 10;
    mb.vertex(tail.x(), tail.y());
    mb.vertex(b.position.x(), b.position.y());
  }
  mb.end();
  glLineWidth(2.5f);
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

void GLMiniStation::draw(bool minimap) const {
  float px = position.x(), py = position.y();

  if (minimap && alive) {
    map_body_mesh.draw_at(px, py, 0.0f);
  } else if (alive) {
    glLineWidth(2.0f);
    body_mesh.draw_at(px, py, outer_rotation);
    float inner_model[16]; mat4_identity(inner_model);
    mat4_translate(inner_model, inner_model, px, py, 0.0f);
    mat4_rotate_z(inner_model, inner_model, inner_rotation);
    mat4_scale(inner_model, inner_model, 0.8f, 0.8f, 1.0f);
    body_mesh.draw_with_model(inner_model);
  }

  if (!minimap) {
    draw_bullets();

    if (!debris.empty()) {
      static MeshBuilder mb;
      static Mesh mesh;
      mb.clear();
      mb.begin(GL_POINTS);
      for (const auto &d : debris) {
        mb.color(1.0f, 0.7f, 0.2f, d.aliveness());
        mb.vertex(d.position.x(), d.position.y());
      }
      mb.end();
      mesh.upload(mb, GL_DYNAMIC_DRAW);
      mesh.draw(3.0f);
    }
  }
}
