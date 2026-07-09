#ifndef LANCE_H
#define LANCE_H

#include "base.h"
#include "SDL.h"
#include "SDL_mixer.h"

namespace Weapon {
  // Primary weapon: a single instantaneous energy pulse at full length. The
  // weapon only consumes ammo and requests the pulse; Ship::fire_lance_pulse()
  // (which has the collision grid) ray-marches it: every killable asteroid
  // along the line dies, the pulse mirror-reflects off surfaces that reflect
  // bullets (reflective asteroids, armoured faces, phased ghosts) carrying its
  // remaining distance, and is blocked by anything it cannot destroy.
  // Limited ammo, refilled by LancePickup.
  class Lance : public Base {
  public:
    // Total pulse path length; matches the pierce beam bolt's travel
    // (BOLT_SPEED * BOLT_TTL in weapon/beam.cpp).
    static const float RANGE;

    Lance(Ship *ship);
    ~Lance();

    void shoot(bool on = true) override;
    void step(int delta) override;

  private:
    void fire();

    int time_until_next_shot, time_between_shots;

    Mix_Chunk *shoot_sound = NULL, *empty_sound = NULL;
  };
}

#endif
