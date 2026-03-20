#ifndef GIGA_MINE_H
#define GIGA_MINE_H

#include "base.h"
#include <SDL_mixer.h>

namespace Weapon {
  class GigaMine : public Base {
  public:
    GigaMine(Ship *ship);
    ~GigaMine();

    void shoot(bool on = true);
    void step(int delta);

    Mix_Chunk *mine_sound = NULL, *empty_sound = NULL;
  };
}

#endif
