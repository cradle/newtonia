#ifndef GL_STATION_H
#define GL_STATION_H

#include <list>
#include "glship.h"
#include "mesh.h"
#include "object.h"
#include "wrapped_point.h"
#include "savegame.h"

using namespace std;

class GLStation : public Ship {
public:
  GLStation(const Grid &grid, list<GLShip*>* objects, list<GLShip*>* targets, list<Object*>* asteroids = NULL, float aim_lead = 0.0f, int generation = 0, list<Object*>* missile_ships = NULL);
  virtual ~GLStation();

  void draw(bool minimap = false) const;
  void step(float delta, const Grid &grid);
  // Net client: motion + ring spin only — deployment, waves and
  // collisions are host-authoritative (restore_state mirrors them).
  void net_client_step(float delta);
  // void collide(Ship *ship) const;
  void reset(bool was_killed = true);
  int level() const;
  void hit();
  void destroy();

  // Gen-19+ hardening (the ARMOURED STATION, level 20): a bright shield
  // arc rotating with the outer ring deflects incoming fire — the
  // armoured-asteroid vocabulary at boss scale, taught at gen 7 and
  // reinforced by the gen-16 elites. The gap is the weak window, so the
  // plain gun still kills it (locked late-game design rule); health is
  // unchanged — the difficulty is timing and positioning, not sponge.
  // Everything derives from state that already exists: presence from
  // `generation` (the station is rebuilt every rollover, and both the
  // save ctor and the net-client replica pass it), the arc angle from
  // `outer_rotation` (stepped, saved and replicated since v1) — so no
  // savegame or PROTO change.
  bool armoured() const;             // generation >= 19
  float arc_coverage_deg() const;    // 240 at gen 19, widening; gap never < 60
  float arc_center_deg() const;      // = outer_rotation (draw_at degrees)
  // Whether an impact at this point lands on the shield arc. The test is
  // pure bearing-from-centre vs the arc span — the same convention the
  // arc mesh is drawn with (local angle 0 = +x, rotated by draw_at), so
  // the deflection can never disagree with the picture.
  bool deflects(const WrappedPoint &impact) const;
  // Destroying an ARMOURED station pays a bounty (plain gen-14..18
  // stations stay achievement-only); awarded by GLGame at each kill site,
  // hazard_bounty-style.
  int bounty() const;                // REWARD when armoured, else 0
  static const int REWARD;

  Save::Station capture_state() const;
  void restore_state(const Save::Station &s, const Grid &grid);

  int health;

private:
  Mesh body_mesh, map_body_mesh;
  Mesh arc_mesh;  // gen-19+ shield arc (built in the ctor when armoured)
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;

  std::list<GLShip*>* objects, *targets;
  std::list<Object*>* asteroids;
  int ships_this_wave, max_ships_per_wave, extra_ships_per_wave, ships_left_to_deploy;
  int time_until_next_ship, time_between_ships;
  bool deploying, redeploying;
  int wave, difficulty;
  // How much of the perfect intercept lead this generation's wave ships aim
  // with (GLGame::hostile_aim_lead; the station is recreated each rollover,
  // so the value tracks the generation with no saved state). Handed to each
  // deployed GLEnemy's Follower at spawn.
  float aim_lead;
  // The generation this station was built for (same recreated-per-rollover
  // contract as aim_lead — never persisted). Drives wave COMPOSITION:
  // interceptors join from generation 15, their per-wave allotment growing
  // with it (wave_interceptors). NOT the `difficulty` member below — that
  // counter only moves past the 50-ship wave cap and resets with the
  // station every level, so it never actually climbs.
  int generation;
  // The game's missile-homing list (GLGame::ship_objects). Every hull this
  // station creates joins it and every hull it deletes leaves it, so
  // player missiles home on wave ships exactly like they do on the
  // stations; GLGame's death sweep handles the ordinary removals. NULL in
  // tests that build a bare station.
  list<Object*>* missile_ships;
  // How many of this wave's ships deploy as interceptors: none before
  // gen 15, then 1 + (generation - 15), capped at half the wave (rounded
  // up) so the standard line always outnumbers-or-matches the vanguard.
  int wave_interceptors() const;
  // How many of this wave's ships deploy as rammers: none before gen 20,
  // then 1 + (generation - 20), capped by what the interceptor vanguard
  // leaves minus the reserved standard slot (waves of 3+, same scheme as
  // the bombers'). The NEWEST specialist outranks the older ones for the
  // remaining slots — the gen-20 intro's promise must show up by wave 2,
  // exactly the bomber's debut rule at 17 — which is why wave_bombers()
  // subtracts this count and not the other way round.
  int wave_rammers() const;
  // How many of this wave's ships deploy as bombers: none before gen 17,
  // then 1 + (generation - 17), capped by what the interceptor vanguard
  // AND the rammer charge leave MINUS one reserved standard slot in waves
  // of 3+ ships (so the green line never vanishes from the late-game
  // mix); waves of 1-2 skip the reservation so the gen-17 bomber still
  // debuts by wave 2.
  int wave_bombers() const;
};

#endif
