#ifndef DEFAULT_H
#define DEFAULT_H

#include "base.h"
#include "SDL.h"
#include "SDL_mixer.h"

class Point;

namespace Weapon {
  class Default : public Base {
  public:
    Default(Ship *ship, bool automatic = false, int level = 0, float accuracy = 0.1f, int time_between_shots = 100);
    ~Default();

    void shoot(bool on = true);
    void step(int delta);

  private:
    void fire();
    void fire_shot(Point direction);

    bool automatic;
    float accuracy;
    int time_until_next_shot, time_between_shots;
    int level;

    Mix_Chunk *shoot_sound = NULL, *empty_sound = NULL;
  };
}

#endif
