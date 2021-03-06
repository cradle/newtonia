#ifndef ASTEROID_H
#define ASTEROID_H

#include "composite_object.h"
#include "asteroid_drawer.h"
#include <cstdlib>
#include <SDL.h>
#include <SDL_mixer.h>

class Asteroid : public CompositeObject {
public:
  Asteroid(bool invincible);
  Asteroid(Asteroid const *mother);
  virtual ~Asteroid();

  void add_children(list<Asteroid*> *objects);
  virtual bool kill();

  friend class AsteroidDrawer;

  static int num_killable;

  const static int max_radius;

  static Mix_Chunk *explode_sound, *thud_sound;

private:
  const static int max_speed;
  const static int radius_variation;
  const static int minimum_radius;
  const static int max_rotation;

  bool children_added, killed;
};
#endif
