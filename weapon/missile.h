#ifndef MISSILE_H
#define MISSILE_H

#include "base.h"
#include "../object.h"
#include "../point.h"
#include "../wrapped_point.h"
#include <deque>
#include <list>
#include <memory>
#include <stdint.h>
#include <SDL_mixer.h>

class Grid;

struct MissileShot : public Object {
  Point facing;
  float thrust;
  float time_left;
  std::deque<WrappedPoint> trail;
  std::shared_ptr<int> sound_handle;
  // Net client, locally-fired missile the host has not echoed back yet —
  // see Ship::NET_DEPLOY_GRACE. Counts down one per snapshot apply;
  // while it is nonzero the snapshot rebuild holds this missile instead
  // of reading its absence as a host-side detonation. Transient; never
  // serialized (host-echoed copies are confirmed by construction).
  uint8_t net_unconfirmed = 0;

  static const float TIME_TO_LIVE;
  static const float INITIAL_SPEED;
  static const float ACCELERATION;
  static const float MAX_THRUST;
  static const float MAX_SPEED;
  static const float SEEK_RANGE;
  static const float TURN_RATE;
  static const int   TRAIL_LENGTH;

  MissileShot(WrappedPoint pos, Point facing_dir, Point base_velocity);
  // seek_players=false (friendly fire off) skips player ships in the seek
  // scan — a co-op missile must not hunt the partner. Asteroid candidates
  // come from the grid's radius query (Grid::query_radius — the missile
  // re-seeks every 8 ms step, so it was the hottest of the full-list
  // walks); `grid` is required. Ships arrive as a list — it is tiny.
  void step_missile(int delta, const Grid *grid,
                    std::list<Object*> *ships = nullptr,
                    bool seek_players = true);
  bool is_alive() const { return time_left > 0; }
};

namespace Weapon {
  class Missile : public Base {
  public:
    Missile(Ship *ship);
    ~Missile();

    void shoot(bool on = true);
    void step(int delta);
    void set_asteroids(std::list<Object*> *a) { asteroids = a; }
    void set_ship_targets(std::list<Object*> *s) { ship_targets = s; }

    Mix_Chunk *fly_sound = NULL, *empty_sound = NULL;
    std::weak_ptr<int> fly_channel_handle;
  private:
    std::list<Object*> *asteroids = nullptr;
    std::list<Object*> *ship_targets = nullptr;
  };
}

#endif
