#ifndef GL_MINI_STATION_H
#define GL_MINI_STATION_H

#include <list>
#include "glship.h"
#include "mesh.h"
#include "object.h"
#include "ship.h"
#include "grid.h"
#include "savegame.h"

using namespace std;

// A small roaming space station that appears once the black hole has been
// introduced. It drifts across the world at a steady speed in a single random
// direction, passing straight through asteroids (no body collision). Every few
// seconds it fires a bullet at the nearest player; those bullets destroy
// asteroids exactly like player shots but award the players nothing. A single
// player shot destroys it for a fixed point reward (handled in GLGame).
class GLMiniStation : public Ship {
public:
  GLMiniStation(const Grid &grid, list<GLShip*>* players, list<Object*>* asteroids = NULL);
  virtual ~GLMiniStation();

  void draw(bool minimap = false) const;
  void step(float delta, const Grid &grid);
  void destroy();   // kill in one hit, scatter debris

  Save::MiniStation capture_state() const;
  void restore_state(const Save::MiniStation &s);

  static const int REWARD = 1000;  // points awarded to the player that kills it

private:
  void fire_at_nearest_player();
  void draw_bullets() const;

  Mesh body_mesh, map_body_mesh;
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;

  list<GLShip*>* players;

  float time_until_next_shot;
  static const float SHOOT_INTERVAL;   // ms between shots
  static const float DRIFT_SPEED;      // world units per ms
};

#endif
