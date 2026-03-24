#ifndef MISSILE_H
#define MISSILE_H

#include "base.h"
#include "../object.h"
#include "../point.h"
#include "../wrapped_point.h"
#include <deque>
#include <list>
#include <memory>
#include <SDL_mixer.h>

struct MissileShot : public Object {
  Point facing;
  float thrust;
  float time_left;
  std::deque<WrappedPoint> trail;
  std::shared_ptr<int> sound_handle;

  static const float TIME_TO_LIVE;
  static const float INITIAL_SPEED;
  static const float ACCELERATION;
  static const float MAX_THRUST;
  static const float MAX_SPEED;
  static const float SEEK_RANGE;
  static const float TURN_RATE;
  static const int   TRAIL_LENGTH;

  MissileShot(WrappedPoint pos, Point facing_dir, Point base_velocity);
  void step_missile(int delta, std::list<Object*> *asteroids, std::list<Object*> *ships = nullptr);
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
