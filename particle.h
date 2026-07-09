#ifndef PARTICLE_H
#define PARTICLE_H

#include <stdint.h>

#include "object.h"
#include "point.h"
#include "wrapped_point.h"

class Particle : public Object {
public:
  Particle(const Point position, const Point direction, float time_to_live, float rotation_speed = 0.0f);
  virtual ~Particle() {};

  virtual void step(int delta);
  bool is_alive() const;
  float aliveness() const;

  // PROTO 14 shot identity: assigned by the firing client, carried by
  // the host's exact clone, referenced by MSG_HIT for precise consume.
  // 0 = not a reported shot (debris, host-native bullets).
  uint32_t net_id = 0;

  // PROTO 18 wire flags (snapshot per-bullet byte): bit0 kills_invincible,
  // bit1 has_trail, bit2 piercing — the free nx_* serializers are not
  // friends, so the presentation-critical flags cross the wire via these.
  uint8_t net_flags() const {
    return (uint8_t)((kills_invincible ? 1 : 0) | (has_trail ? 2 : 0) |
                     (piercing ? 4 : 0));
  }
  void set_net_flags(uint8_t f) {
    kills_invincible = (f & 1) != 0;
    has_trail = (f & 2) != 0;
    piercing = (f & 4) != 0;
  }

  //TODO: Fix encapsulation, GLShip -> ParticleDrawer etc.
  friend class GLShip;
  friend class Ship;
  friend class GLTrail;

private:
  float time_to_live, time_left;
  bool world_bullet = false;
  bool has_trail = false;
  bool kills_invincible = false;
  bool piercing = false;        // beam bolt: survives asteroid kills, ploughs through a line
  int trail_timer = 0;
  // Net client: this bullet already sprayed its cosmetic asteroid-impact
  // debris (Ship::net_cosmetic_impacts) — a replicated copy crossing an
  // asteroid between snapshot corrections must not strobe. Transient;
  // never serialized (rebuilt copies default to false).
  bool net_sparked = false;
};

#endif /* PARTICLE_H */
