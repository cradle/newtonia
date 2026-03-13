#ifndef MISSILE_H
#define MISSILE_H

#include "base.h"
#include "../object.h"
#include "../point.h"
#include "../wrapped_point.h"
#include <deque>
#include <list>
#include <SDL_mixer.h>

struct MissileShot : public Object {
  Point facing;
  float thrust;
  float time_left;
  std::deque<WrappedPoint> trail;

  static const float TIME_TO_LIVE;
  static const float INITIAL_SPEED;
  static const float ACCELERATION;
  static const float MAX_THRUST;
  static const float MAX_SPEED;
  static const float SEEK_RANGE;
  static const float TURN_RATE;
  static const int   TRAIL_LENGTH;

  MissileShot(WrappedPoint pos, Point facing_dir, Point base_velocity);
  void step_missile(int delta, std::list<Object*> *asteroids);
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

    Mix_Chunk *fly_sound = NULL, *empty_sound = NULL;
  private:
    std::list<Object*> *asteroids = nullptr;
  };
}

#endif
