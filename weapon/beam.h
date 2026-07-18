#ifndef BEAM_H
#define BEAM_H

#include "base.h"
#include "SDL.h"
#include "SDL_mixer.h"

namespace Weapon {
  // Primary weapon: fires a single fast bolt that pierces straight through a
  // line of asteroids (marked `piercing` on the Particle) rather than stopping
  // at the first hit. Limited ammo, refilled by BeamPickup.
  class Beam : public Base {
  public:
    Beam(Ship *ship);
    ~Beam();

    void shoot(bool on = true) override;
    void step(int delta) override;

  private:
    void fire();

    int time_until_next_shot, time_between_shots;

    Mix_Chunk *shoot_sound = NULL, *empty_sound = NULL;
  };
}

#endif
