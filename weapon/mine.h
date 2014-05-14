#ifndef MINE_H
#define MINE_H

#include "base.h"
#include <SDL_mixer.h>

namespace Weapon {
  class Mine : public Base {
  public:
    Mine(Ship *ship);
    ~Mine();

    void shoot(bool on = true);
    void step(int delta);

    Mix_Chunk *mine_sound = NULL, *empty_sound = NULL;
  };
}

#endif
