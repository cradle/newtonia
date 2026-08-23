#ifndef FOLLOWER_H
#define FOLLOWER_H

class Object;
#include "behaviour.h"
#include "wrapped_point.h"
#include <list>

using namespace std;

class Follower : public Behaviour {
public:
  // GUNNER is every ordinary wave ship: close to 600 and burst-fire the
  // gun. BOMBER is the artillery piece: stand off (thrust cuts inside
  // STANDOFF_RANGE so it drifts rather than diving in), engage from
  // BOMBER_RANGE, and lob mortar shells (Ship::fire_bomb) instead of
  // shooting.
  enum Mode { GUNNER, BOMBER };

  Follower(Ship *ship);
  Follower(Ship *ship, list<Object *> *targets);
  Follower(Ship *ship, list<Object *> *targets, list<Object *> *asteroids, float difficulty = 0.0f, float aim_lead = 0.0f, int shoot_interval_ms = 3000, Mode mode = GUNNER);
  virtual ~Follower();

  virtual void step(int delta);
  void lock_now();  // skip initial delay and acquire target immediately

private:
  void common_init();
  void lock_step(int delta);
  void lock_nearest_target();
  void burst_shooting_step(int delta, float angle, const WrappedPoint &target_point);
  bool compute_avoidance(float &avoidance_angle, float &avoidance_strength);

  list<Object *> *targets;
  list<Object *> *asteroids;  // non-owning pointer
  float difficulty;
  float aim_lead;  // 0 = steer/shoot at the target now, 1 = perfect intercept
  // Ms between shot windows (burst_shooting_step). 3000 for the standard
  // wave ship; the interceptor's tighter cadence (1500) is its real
  // weapon — see the late-game design rule in CLAUDE.md: pressure comes
  // from being shot at more while it is near, never from a speed the
  // player cannot escape with plain thrust. Host-simulated only (client
  // replicas never run behaviours), so this needs no serialization.
  int shoot_interval_ms;
  Mode mode;
  int time_until_next_lock, time_between_locks;
  int shoot_timer;
  // Keep-distance: inside BREAK_RANGE of the target the ship breaks off —
  // turns away and thrusts back out — until it has opened RESUME_RANGE
  // again (hysteresis, or the boundary flip-flops every tick). Wave ships
  // used to press the approach all the way in and end their lives as
  // suicide rams; rams still happen on momentum, never as the steady
  // state (a deliberate rammer can be a future ship variant). Host-only
  // transient like shoot_timer — never serialized.
  bool backing_off;
  Object *target;
};

#endif
