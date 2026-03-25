#ifndef ASTEROID_H
#define ASTEROID_H

#include "composite_object.h"
#include "asteroid_drawer.h"
#include <cstdlib>
#include <SDL.h>
#include <SDL_mixer.h>

class Asteroid : public CompositeObject {
public:
  Asteroid(bool invincible, bool invisible = false, bool reflective = false, bool teleporting = false, bool quantum = false);
  Asteroid(Asteroid const *mother);
  virtual ~Asteroid();

  bool add_children(list<Asteroid*> *objects);
  virtual bool kill() override;
  virtual void step(int delta) override;
  virtual bool contains(Point p, float r = 0.0f) const override;
  virtual float effective_radius() const override { return radius * max_vertex_offset; }
  Point surface_normal(Point entry, Point incoming_dir) const;
  bool segment_hit(Point a, Point b, float &t_hit) const;

  friend class AsteroidDrawer;

  static int num_killable;

  const static int max_radius;

  static Mix_Chunk *explode_sound, *thud_sound;

  static void free_sounds();

  bool invisible;
  bool reflective;
  bool teleporting;         // true = this is a teleporting asteroid type
  bool teleport_vulnerable; // true = has teleported, now vulnerable for limited time
  bool teleport_pending;    // true = needs to be relocated by game loop this tick
  float teleport_angle;     // direction of the in-asteroid indicator (radians)
  int vulnerable_time_left; // countdown in ms while teleport_vulnerable is true

  bool quantum;             // true = quantum asteroid (observer-dependent behavior)
  bool quantum_observed;    // true = currently observed by a player (collapsed, killable)
  float quantum_base_speed; // base speed magnitude for observation state transitions

private:
  const static int max_speed;
  const static int radius_variation;
  const static int minimum_radius;
  const static int max_rotation;

  bool children_added, killed;
  float vertex_offsets[9]; // per-vertex radius multipliers for irregular shape
  float max_vertex_offset;  // bounding circle scale: max of vertex_offsets
};
#endif
