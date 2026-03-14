#ifndef SHIELD_H
#define SHIELD_H

#include "base.h"
#include <SDL_mixer.h>

namespace Weapon {
  class Shield : public Base {
  public:
    Shield(Ship *ship);
    ~Shield();

    void shoot(bool on = true);
    void step(int delta);

    Mix_Chunk *empty_sound = NULL;
  };
}

#endif
