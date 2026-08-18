#ifndef GL_STATION_H
#define GL_STATION_H

#include <list>
#include "glship.h"
#include "mesh.h"
#include "object.h"
#include "wrapped_point.h"
#include "savegame.h"

using namespace std;

class GLStation : public Ship {
public:
  GLStation(const Grid &grid, list<GLShip*>* objects, list<GLShip*>* targets, list<Object*>* asteroids = NULL, float aim_lead = 0.0f);
  virtual ~GLStation();

  void draw(bool minimap = false) const;
  void step(float delta, const Grid &grid);
  // Net client: motion + ring spin only — deployment, waves and
  // collisions are host-authoritative (restore_state mirrors them).
  void net_client_step(float delta);
  // void collide(Ship *ship) const;
  void reset(bool was_killed = true);
  int level() const;
  void hit();
  void destroy();

  Save::Station capture_state() const;
  void restore_state(const Save::Station &s, const Grid &grid);

  int health;

private:
  Mesh body_mesh, map_body_mesh;
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;

  std::list<GLShip*>* objects, *targets;
  std::list<Object*>* asteroids;
  int ships_this_wave, max_ships_per_wave, extra_ships_per_wave, ships_left_to_deploy;
  int time_until_next_ship, time_between_ships;
  bool deploying, redeploying;
  int wave, difficulty;
  // How much of the perfect intercept lead this generation's wave ships aim
  // with (GLGame::hostile_aim_lead; the station is recreated each rollover,
  // so the value tracks the generation with no saved state). Handed to each
  // deployed GLEnemy's Follower at spawn.
  float aim_lead;
};

#endif
