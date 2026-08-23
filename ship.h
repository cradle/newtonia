#ifndef SHIP_H
#define SHIP_H

#include "composite_object.h"
#include "point.h"
#include "particle.h"
#include "weapon/missile.h"
#include "weapon/shock.h"
#include "weapon/turret.h"
#include "grid.h"
#include "black_hole.h"
#include "savegame.h"
#include <list>
#include <vector>
#include <SDL.h>
#include <SDL_mixer.h>

struct Shockwave {
  Point position;
  float radius;
  float prev_radius;
  float max_radius;
  float speed;        // units per ms
  float time_left;   // ms
  bool  is_nova;     // nova shockwaves: use wrapped distance, no nova-charge feedback

  Shockwave(Point pos, float max_r, float spd, float duration, bool nova = false)
    : position(pos), radius(0.0f), prev_radius(0.0f),
      max_radius(max_r), speed(spd), time_left(duration), is_nova(nova) {}

  bool alive() const { return time_left > 0.0f && radius < max_radius; }

  void step(float delta) {
    prev_radius = radius;
    radius += speed * delta;
    time_left -= delta;
    if(radius > max_radius) radius = max_radius;
  }
};

// A fired lance pulse, kept only for rendering: the polyline the ray-march
// traced (gun -> reflections -> end point), fading out over ttl ms.
struct LancePulse {
  std::vector<Point> points;
  float time_left;
  float ttl;
  float aliveness() const { return ttl > 0.0f ? time_left / ttl : 0.0f; }
};

class Behaviour;
class Object;
namespace Weapon { class Base; }

using namespace std;

class Ship : public CompositeObject {
  public:
    Ship(const Grid &grid, bool has_friction = true);
    virtual ~Ship();

    void puts(); //TODO: convert into iostream operator

    using CompositeObject::step;
    virtual void step(float delta, const Grid &grid);

    void rotate_left(bool on = true);
    void rotate_right(bool on = true);
    void thrust(bool on = true);
    void reverse(bool on = true);
    // Re-level the looping thruster hum from the current thrust/rotate state
    // and sound_volume_scale. The control calls do it on every change; GLGame
    // also runs it per tick, because the listener distance folded into the
    // scale keeps moving while the keys are simply held down.
    void update_boost_volume();
    void update_missile_fly_volumes();
    void shoot(bool on = true);
    void shoot_weapon(bool on = true);
    void fire_secondary(bool on = true);
    void boost();
    int multiplier() const;

    float heading() const;
    bool is_alive() const;
    virtual bool is_removable() const;

    using Object::collide;
    static void collide(Ship *first, Ship *second);
    // bool collide_object(Object *other);
    void collide_grid(Grid &grid, int delta);
    void collide_bullets_with_asteroids(const Grid &grid, int delta);
    void collide(Ship *other);

    //TODO: make friends with glship
    int score;
    int lives, kills, kills_this_life;
    int asteroid_kills = 0;  // asteroids destroyed this game (achievements; saved)
    int enemy_kills = 0;     // enemy ships destroyed this game (achievements; saved)
    // Locally-controlled player ship: gates achievement and lifetime-stat
    // attribution (ACHIEVEMENTS.md §4). Set by GLGame when creating player
    // ships; stays false for enemies, stations, and future remote netplay peers.
    bool is_local_player = false;
    bool died_this_generation = false;  // reset by GLGame on generation rebuild
    uint32_t weapons_fired_mask = 0;    // bit = (int)Save::WeaponEntry::Kind fired this game
    int nova_charge;       // charge points accumulated toward next bomb (0–9)
    int nova_kill_counter; // asteroid kills accumulated toward next charge pickup drop (0–99)
    std::vector<Point> nova_drops_pending;  // pickup spawn positions; GLGame reads and clears each frame
    //TODO: Make this go away, it's wrong
    float radius_squared;
    bool thrusting, reversing, boosting;
    float sound_volume_scale = 1.0f;  // 0=silent, 1=full; set by GLGame for enemy AI
    // "Cues about THIS ship, for whoever is flying it": the shield hum and
    // god-mode music loops, and the respawn countdown tics. Not a volume —
    // a question about whose ship it is. These used to test
    // sound_volume_scale >= 1.0f, which answered it only by accident, back
    // when the attenuation curve peaked at literally zero distance. Now that
    // it holds full volume across the whole visible screen, anything you can
    // see would pass that test, so the question is asked directly. GLGame
    // owns it like the scale: the ships whose screen this is get true, the
    // online peer and the world actors false.
    bool sound_own_cues = true;

    // Order in which players ran OUT of lives — a monotonic stamp taken the
    // moment a ship dies with none left, so the revive pickup can hand the
    // life to whoever has been waiting longest instead of to the lowest
    // seat. 0 means "not out", or "out before this counter saw it" (a
    // resumed save, which the pickup treats as oldest).
    unsigned out_order() const { return out_order_; }
    // Back from fully out: one life and a fresh respawn countdown. Clears
    // the stamp here so a later death is ordered afresh — the two must move
    // together, which is why this isn't done at the call site.
    void revive_one_life() {
      lives = 1;
      time_until_respawn = respawn_time;
      out_order_ = 0;
    }

    // Respawn-countdown tic, deferred: step() flags the second-boundary
    // crossing here (1 = tic, 2 = final-second tic_low) instead of playing
    // it, and GLGame drains the flags ONCE per step across all players —
    // simultaneous joins/deaths sync the countdowns, and N in-phase copies
    // of the same unattenuated cue sum into one very loud beep on the
    // shared speakers (4P field bug #1). Same-step crossings collapse to a
    // single audible tic per kind; staggered countdowns still tic apart.
    int respawn_tic_pending = 0;
    void flush_respawn_tic(bool &tic_played, bool &low_played);

    // Extrapolation damping for a ship whose rotation this machine is
    // GUESSING rather than driving: the client (and replay playback) turns
    // a replicated ship from the held-rotation flag in the 10 Hz snapshot,
    // so it keeps turning until the next one says stop — up to 100 ms of
    // rotation at 286 deg/s. The reconcile then unwinds that overshoot, and
    // a reversal at the end of every turn is far more noticeable than a
    // small steady lag. Turning slower than real makes the per-snapshot
    // correction point FORWARD instead, which reads as continuous motion.
    // 1.0 (no damping) everywhere the rotation is known rather than
    // guessed: the local pilot, and the host applying INPUT flags that
    // arrive far faster than snapshots.
    float net_rotation_damp = 1.0f;
    // Time-slow pickup compensation (GLGame::start_time_slow): while the
    // world's wall-clock rate is divided by the slow factor, the collector's
    // per-step rotation is multiplied by it, so turning FEELS unchanged in
    // wall time while everything else — thrust, fire rate, the world — runs
    // slow. 1.0 everywhere except the collecting ship during the effect.
    float time_slow_rotation_comp = 1.0f;
    // Analog scale factors (0.0–1.0); set by joystick/controller input
    float rotation_scale;  // scales rotation_force (default 1.0)
    float thrust_analog;   // scales thrust_force   (default 1.0)
    float reverse_analog;  // scales reverse_force  (default 1.0)

    //TODO: make friends with gltrail (or some other way around these public)
    WrappedPoint tail() const;
    Point facing;
    // Angular twin of Object::net_pose_err (radians): the facing correction
    // a replicated ship owes authority, drained by net_smooth_facing so a
    // snapshot's rotation lands as a glide instead of a snap. Zero on the
    // local ship, whose facing is authoritative (PROTO 12).
    float net_facing_err = 0.0f;

    //TODO: somehow get around this public for glstation
    // Returns whether the ship actually died (false = shield/invincible).
    bool kill_stop();

    // Replay bootstrap (REPLAY.md R2): return a restore-resurrected ship to
    // its recorded dead-in-countdown state with NONE of kill()'s theatre —
    // no CompositeObject::explode() hull debris, no explode sound, no boom
    // relay, no detonate flash. Initial state is state, not an event.
    void quiet_unspawn();

    std::vector<Particle> bullets, mines, giga_mines, bullet_trails;
    std::vector<MissileShot> missiles;
    std::vector<TurretDrone> turrets;
    std::vector<Shockwave> shockwaves;
    std::vector<LancePulse> lance_pulses;
    bool lance_pulse_pending = false;  // set by Weapon::Lance; consumed in step()
    std::vector<ShockBolt> shocks;

    enum Rotation {
      LEFT = 1,
      NONE = 0,
      RIGHT = -1
    };
    Rotation rotation_direction;
    bool still_rotating_left, still_rotating_right;

    // Heat
    float temperature_ratio();
    float max_temperature, critical_temperature, temperature, explode_temperature;

    // I need friends for views
    // Timings
    int respawn_time, time_until_respawn;

    //FIX: friends
    int time_left_invincible;

    // Netplay: wrapping one-shot counters bumped by the action methods below;
    // the online client samples them each tick into INPUT messages (the host
    // applies the deltas). Meaningless offline. See NETPLAY.md.
    uint8_t net_boost_count = 0, net_next_weapon_count = 0,
            net_next_secondary_count = 0, net_teleport_count = 0;
    // Netplay: bumped whenever the simulation moves this ship
    // discontinuously (respawn, teleport). Travels in the host's snapshot
    // extras; a change tells the client the new pose is absolute — snap to
    // it instead of blending the prediction correction. See NETPLAY.md.
    uint8_t net_warp_count = 0;

    // Serialisation: capture/restore the full player state including weapons.
    Save::Player capture_state() const;
    void restore_state(const Save::Player &p, const Grid &grid);
    // Netplay fast path: true when the live roster matches the snapshot's,
    // so restore_state can update ammo/selection in place, no rebuild.
    bool net_weapons_roster_matches(const Save::Player &p) const;

    void add_behaviour(Behaviour *b);
    void disable_behaviours();
    void disable_weapons();

    void next_weapon();
    void previous_weapon();
    void next_secondary_weapon();
    // True while any secondary is equipped (the list empties when the last
    // one runs dry — see fire_secondary). Gates the touch OSD's mine button.
    bool has_secondary() const { return !secondary_weapons.empty(); }
    void add_weapon(int weapon_index);
    // Random gun-drop pool: draws from weapon_configs[] minus the slow
    // semi-auto rows — one shot per trigger pull AND a 200ms re-press limit
    // made those a downgrade the moment a pickup swapped the player onto one.
    static int random_drop_weapon_index();
    void add_mine_ammo(int amount);
    void add_giga_mine_ammo(int amount);
    void add_turret_ammo(int amount);  // secondary: deployable sentry drones
    void add_missile_ammo(int amount);
    void add_shield_ammo(int amount);
    void add_beam_ammo(int amount);
    void add_lance_ammo(int amount);
    void add_shock(int amount);  // primary weapon (chain lightning)
    // Debug/testing: grant every primary gun variant and every secondary at
    // `ammo` rounds (Nova stays at its design cap). Used by the NEWTONIA_ALL_WEAPONS
    // cheat flag. God mode is deliberately excluded — it hijacks the primary slot
    // and blocks weapon cycling.
    void give_all_weapons(int ammo);
    void add_god_mode(int duration_ms = 10000);
    int god_mode_time_remaining() const;
    bool shield_active() const;
    void add_nova_charge(int n);   // call on every asteroid kill
    void add_nova_ammo(int amount);
    void nova_detonate();
    int nova_ammo() const;
    void set_shield_hum(bool on);
    // Halt all continuous per-ship loops (shield hum, god-mode music, boost)
    // — used on a net client's terminal disconnect so a snapshot-driven loop
    // isn't left stuck on when the host leaves and snapshots stop.
    void silence_loops();
    // Net client: respawn() runs inside every snapshot restore (10 Hz),
    // and its hum start-then-halt leaked audible blips. With this set the
    // hum is started ONLY by explicit set_shield_hum calls (the snapshot
    // extras decide), never by respawn itself.
    static bool net_quiet_respawn;
    // Deployed projectiles (mines, giga mines, missiles) are spawned
    // locally the instant the trigger is pulled — the pilot gets no
    // latency — but on a net CLIENT the host owns their lifecycle and
    // echoes them back a round trip later, and the snapshot rebuild
    // replaces the list wholesale (nx_read_projectiles in glgame.cpp).
    // A just-fired one is therefore missing from the host's set for the
    // first apply or two, which the vanish detection below read as "the
    // host detonated it": every client-fired missile blew up at the
    // muzzle and then the host's echo flew off — Glenn's "double missile
    // where one explodes instantly". Ship::fire_secondary stamps each
    // fresh deploy with this many applies of grace; the rebuild holds an
    // unmatched one (and never explodes it) until the count runs out,
    // by which point the echo has either adopted it or the host never
    // fired it at all (ammo desync) and it goes quietly. The pre-decrement
    // makes 5 stamps = 4 unmatched applies of grace, ~400 ms at the 10 Hz
    // apply rate — well past any playable round trip when both legs
    // degrade together. Note the count runs on host SNAPSHOTS RECEIVED,
    // not round trips: under asymmetric loss (INPUTs delayed while
    // snapshots keep flowing) the grace can expire before the press ever
    // reaches the host, and the deploy vanishes then reappears as the
    // late echo. Accepted: the alternative (pausing the count when
    // INPUTs aren't being acked) needs wire changes for a transient
    // cosmetic glitch with no desync — the host stays authoritative.
    //
    // That case has now been OBSERVED, not just reasoned about: on a
    // loaded 4-vCPU CI runner (2026-08-13) missile_net.sh caught one
    // client-fired missile vanishing 160 ms after launch — too early to
    // be any real detonation the driver has on record (closest: 424 ms),
    // too late to be the grace-disabled signature (0-10 ms). One in ~120
    // launches, under load the e2e suite deliberately applies. If the
    // glitch is ever reported from the field rather than from CI, this
    // constant — or the acked-INPUT pause above — is where to start.
    static const uint8_t NET_DEPLOY_GRACE = 5;
    // Net client: a replicated missile vanished mid-flight — the host saw
    // it hit something. Blast + explosion sound at its last position.
    void net_missile_exploded(const Point &pos, const Point &vel);
    // Same for mines / giga mines that vanish from the snapshot early.
    void net_mine_exploded(const Point &pos, const Point &vel);
    void net_giga_mine_exploded(const Point &pos);
    // Turret death/retirement: bookkeeping-side bullet mint (gated like the
    // gun's — see fire_bullet_from_gun) and the debris burst both live on
    // Ship because the drone can't reach bullets/debris itself.
    void fire_turret_bullet(TurretDrone &t);

    // Bomber wave ship (Follower::BOMBER): lob a mortar shell along the
    // facing — a slow, trailed bullet flagged is_bomb. GLGame's fuse pass
    // detonates it into BOMB_FRAGMENTS ordinary bullets at the end of its
    // flight or on player proximity, whichever first. Host-side only (the
    // Follower never runs on clients); the shell and its fragments both
    // replicate through the ordinary enemy-bullet snapshot feed.
    void fire_bomb();
    static const float BOMB_SPEED;        // shell velocity, units/ms
    static const float BOMB_TTL_MS;       // flight time = fixed-range fuse
    static const float BOMB_PROX_RADIUS;  // early fuse: player this close
    static const int   BOMB_FRAGMENTS;    // flak ring size
    static const float BOMB_FRAG_SPEED;
    static const float BOMB_FRAG_TTL_MS;
    void turret_explode(const TurretDrone &t);
    // Shared blast for the two above: the local ship's goes into bullets
    // like the real detonate() (instant local kills + bullet_id-0 claims);
    // the peer's is cosmetic streak debris — its bullets are wholesale-
    // rebuilt from the record every apply, which wiped a bullets blast
    // within ~100 ms.
    void net_blast(const Point &pos, const Point &vel, int count);
    // A nova shockwave appeared in the snapshot: play its boom (the wave
    // itself is replicated; only the audio was host-side).
    void net_nova_arrived();
    // Net client: locally-detected cosmetic bullet-vs-asteroid impacts
    // (debris + thud/ting) for asteroids a plain bullet cannot kill —
    // replaces the host's EV_ROID_THUD/TING impact events (PROTO 10).
    // claim_kills (local ship only): each would-kill consume also pushes
    // the asteroid's net_id to net_kill_claims — the client kills that
    // asteroid locally and sends the host a reliable MSG_HIT claim it
    // honors (PROTO 13 client hit-authority: client kills always count).
    void net_cosmetic_impacts(const Grid &grid, bool claim_kills = false);
    // Client outbox: {asteroid net_id, bullet net_id} pairs the local
    // ship's bullets just visibly killed (see claim_kills above).
    // tick_net_client drains it into MSG_HIT claims.
    struct NetKillClaim { uint32_t ast_id; uint32_t bullet_id; };
    static std::vector<NetKillClaim> net_kill_claims;

    // ---- PROTO 14 client-authoritative shot spawning ------------------
    // net_report_shots (the client's LOCAL ship): every fired bullet is
    // reported (id, spawn pose, exact velocity with spread applied) and
    // sent as reliable MSG_SHOT — the host spawns exact clones instead
    // of re-rolling its own gun sim (independent rand() spread made the
    // two copies of every shot fly on different headings).
    bool net_report_shots = false;
    // Host side, PROTO 12 INPUT: trigger presses the client batched into one
    // INPUT delta (a lost-packet blackout straddling two real semi-auto
    // fires). Ship::step replays one press per step onto the primary so the
    // host's ammo decrements once per CLIENT press, not once per INPUT —
    // collapsing them fired once for N presses and desynced ammo. Fed only
    // by GLGame's INPUT handler; cleared on reset().
    int net_queued_shot_presses = 0;
    // The secondary's twin of the above, same host-side INPUT plumbing. A
    // secondary fires one deploy per press with no auto-fire, so firing
    // once for N batched presses left the client holding mines/missiles it
    // had already spawned and decremented locally — the host never made
    // them, and the client's copies expired unconfirmed (NET_DEPLOY_GRACE).
    int net_queued_secondary_presses = 0;
    uint32_t net_shot_seq = 0;  // id mint for reported shots
    // Last time a reported-clone spawn played the shot sound: a
    // multi-shot trigger pull arrives as one MSG_SHOT per barrel in the
    // same tick, and playing the chunk per clone stacks N identical
    // samples into one very loud bang (the firing side plays ONE sound
    // per pull). Sound-only dedupe — every clone still spawns.
    uint32_t net_clone_sound_ms = 0;
    struct NetShotReport {
      uint32_t id;
      float x, y, vx, vy;
      bool kills_invincible, has_trail, piercing;
    };
    static std::vector<NetShotReport> net_shot_reports;  // client outbox
    // Assign the freshly-fired bullets.back() its id and push its report
    // (no-op unless net_report_shots). Ship is Particle's friend; the
    // weapons that fire bullets are not.
    void net_report_last_bullet();

    // PROTO 18: fired lance pulses (the traced polyline) to report to the
    // peer for its flash + sound. Pushed by fire_lance_pulse on reporting
    // ships (net_report_shots); drained into MSG_LANCE by both roles.
    // Ship* attribution (like replay_lance_flashes): unused on today's
    // 2P wire, but B2's seat-led records need to say whose pulse it was.
    static std::vector<std::pair<const Ship *, std::vector<Point>>>
        net_lance_reports;

    // PROTO 22: completed shock-bolt polylines to report to the peer. A bolt
    // grows with random per-segment jitter, so the receiver must show the
    // firer's EXACT segments, not re-seek its own — pushed once, when the bolt
    // finishes growing, by reporting ships (net_report_shots) in step().
    // Ship* attribution for B2's seat-led records, as net_lance_reports.
    static std::vector<std::pair<const Ship *, std::vector<Point>>>
        net_shock_reports;
    // Build a display-only replica bolt from a received polyline (already
    // grown, just fades; never seeks or kills). Used by the MSG_SHOCK handler.
    void net_receive_shock(const std::vector<Point> &pts);

    // Replay-recorder outboxes (REPLAY.md R2 effect fidelity): the flash-
    // class weapon visuals the snapshots don't carry — lance pulses, shock
    // arcs, nova/giga shockwave rings. Pushed UNCONDITIONALLY at their mint
    // sites (a handful per fight — the net_* twins above stay gated on
    // net_report_shots so wire behaviour is untouched) and drained once per
    // tick by GLGame: recorded when a replay recorder is live, discarded
    // otherwise. Ship* attribution so 2P recordings flash the right ghost.
    struct ReplayRing {
        const Ship *ship;
        float x, y, max_r, speed, duration;
        bool nova;
    };
    static std::vector<std::pair<const Ship *, std::vector<Point>>>
        replay_lance_flashes;
    static std::vector<std::pair<const Ship *, std::vector<Point>>>
        replay_shock_flashes;
    static std::vector<ReplayRing> replay_rings;
    // Gun-shot sound cues (position only — the bullets themselves ride the
    // snapshots; online the SOUND rides the MSG_SHOT echo / EV_WORLD_SHOT,
    // neither of which a solo recording emits). One entry per pew from any
    // ship: players, enemies, god-mode rapid fire, the mini-station.
    // Beam fires cue separately (beam.wav, the piercing-clone rule).
    static std::vector<Point> replay_pews;
    static std::vector<Point> replay_beam_pews;

    // One entry per spawned BULLET (the pew lists above are per trigger
    // pull, and a multi-barrel gun fires several bullets per pull), giving
    // the recorder everything it needs to write an FX_BULLET clone record.
    struct ReplayShot {
      Ship *ship;      // owner; the drain resolves it to a player index
      Point pos, vel;
      uint8_t flags;   // 1 kills_invincible, 2 trail, 4 piercing
    };
    static std::vector<ReplayShot> replay_shots;

    // Lance ship/station hits: the pulse's ray-march only sees asteroids
    // (ships and stations live in GLGame's lists), so every firer parks
    // its traced polyline here. Offline/host firers get the full
    // GLGame::resolve_lance_ship_hits pass after the step; a claim-mode
    // client's pass covers enemy replicas only (instant kill + bullet_id-0
    // claim, PROTO 20) while its station/self/partner hits resolve on the
    // host from the MSG_LANCE polyline.
    std::vector<Point> lance_hit_pending;

    // PROTO 19: authoritative ricochets. The HOST sim pushes a report
    // whenever it bounces an id-carrying bullet off a reflective asteroid
    // or an armoured face (gated by net_report_bounces — host games only);
    // GLGame drains them into MSG_BOUNCE so the client snaps its copy onto
    // the real post-bounce trajectory instead of keeping the local radial
    // approximation from net_cosmetic_impacts.
    struct NetBounceReport { uint32_t id; float x, y, vx, vy; uint8_t flags; };
    static std::vector<NetBounceReport> net_bounce_reports;  // host outbox
    static bool net_report_bounces;
    // Host outbox: achievement unlocks the sim detected for a NON-local
    // ship (ram kills resolve inside Ship code, which cannot send events).
    // GLGame drains it each tick and relays entries belonging to the net
    // remote replica as EV_ACHIEVEMENT (second = Net::AchRelay).
    static std::vector<std::pair<const Ship*, uint8_t>> net_ach_relays;
    // Host outbox: a surviving (shielded) ram killed an asteroid and
    // detonate()d into this ship's bullets. GLGame relays the remote
    // replica's entries as EV_RAM_BLAST — the client skips its own-ship
    // bullet echo, so without the relay the rammer never sees the burst.
    static std::vector<const Ship*> net_ram_blasts;
    // net_claim_kills (the client's LOCAL ship): the lance ray-march
    // predicts kill outcomes without killing locally and queues MSG_HIT
    // claims with bullet_id 0 — the claim drain does the local kills,
    // exactly like bullet claims (PROTO 13). The host player's lance
    // (and offline play) kills directly.
    bool net_claim_kills = false;

    // PROTO 15/16: local bullets vs replicated enemy ships / stations —
    // the ship-shaped twin of net_cosmetic_impacts. Contact consumes the
    // bullet with a spark NOW and pushes a claim; the host applies the
    // damage IFF it consumes the referenced clone. Enemy targets (kind
    // 0) are killed HERE instantly (per-enemy ids make the claim exact
    // and the suppression map stops restores resurrecting them); the
    // stations just thud. targets carries only ALIVE candidates.
    // arc_cov_deg > 0 marks the gen-19 armoured station's shield arc
    // (centred on arc_center_deg): a bullet landing on it bounces
    // locally — ting, white ricochet, no consume, no claim — matching
    // the host's own deflection, which then snaps our copy via
    // MSG_BOUNCE. Pass 0 for everything unarmoured.
    struct NetShipTarget { Object *obj; uint8_t kind; uint32_t id;
                           float arc_center_deg; float arc_cov_deg; };
    struct NetShipHit { uint8_t kind; uint32_t bullet_id; uint32_t target_id;
                        float x, y; };
    static std::vector<NetShipHit> net_ship_hit_claims;  // client outbox
    void net_cosmetic_ship_impacts(const std::vector<NetShipTarget> &targets);
    // PROTO 16: wire identity for ships (enemies today). Minted in the
    // Enemy constructor from the static counter on both sides; the
    // client's mints are overwritten by the host's ids on every apply
    // (replicas are rebuilt from the station record, so identity must
    // be re-stamped each time). 0 = never assigned.
    uint32_t net_ship_id = 0;
    static uint32_t net_next_ship_id;
    // PROTO 25: the seat this PLAYER ship occupies (1..MAX_PLAYERS; P1/host
    // = 1). Stamped by GLGame at every player-creation site (list position
    // + 1 — dense until B4's sparse seats) and by the seat-keyed restore
    // paths. 0 = not a seated player ship (enemies, stations). Partitions
    // the bullet-id mint and keys snapshot ship records.
    uint8_t net_seat = 0;
    // True for ships a player pilots (set by the GLShip/GLCar wrappers);
    // enemies and stations stay false. Missile homing keys off it:
    // with friendly fire off a missile must not seek the partner.
    bool player_ship = false;
    // Mirror of GLGame::friendly_fire on the FIRING ship (kept in sync by
    // GLGame at every toggle/apply site): when false, this ship's missiles
    // skip player_ship targets in their seek scan.
    bool missiles_seek_players = true;
    // net_remote_gun (the HOST's remote ship): the weapon sim keeps its
    // cooldown/ammo/trigger bookkeeping but mints no bullets and plays
    // no shot sound — the real bullets arrive as MSG_SHOT reports via
    // net_spawn_reported_bullet (sound + exact Particle clone).
    bool net_remote_gun = false;
    // quiet: spawn the clone without its gun sound. Replay playback needs
    // this — the recording already carries a separate FX_SHOT cue (one per
    // trigger pull, correctly attenuated against the playback camera), so
    // letting the clone sound off too would double every shot, and a
    // multi-barrel pull would stack one bang per barrel.
    void net_spawn_reported_bullet(uint32_t id, const Point &pos,
                                   const Point &vel, bool kills_inv,
                                   bool trail, bool piercing = false,
                                   bool quiet = false);
    // Start the looping missile-fly sound for replicated missiles (the
    // weapon starts it for locally-fired ones); the handle halts the
    // channel when the last missile holding it is destroyed.
    std::shared_ptr<int> net_start_missile_fly_loop();
    // Host outbox: non-fatal ship-vs-asteroid impacts (debris/ting fire
    // inside collide_grid, which the client never runs). The net host
    // drains this into EV_SHIP_IMPACT; everyone else clears it per tick.
    struct NetShipImpact { const Ship *ship; bool ting; };
    static std::vector<NetShipImpact> net_ship_impacts;
    // Host outbox: gunshots (fire_bullet_from_gun). The net host relays
    // its own player's shots as EV_REMOTE_SHOT and world actors'
    // (enemies, mini-station) as EV_WORLD_SHOT.
    static std::vector<const Ship*> net_shots;
    // Host outbox: ship-class death explosions (kill()). Relayed as
    // EV_WORLD_BOOM for non-player ships; player deaths already reach
    // the client through the snapshot extras.
    static std::vector<const Ship*> net_booms;
    void set_missile_asteroids(std::list<Object*> *asteroids);
    void set_missile_ships(std::list<Object*> *ships);
    void set_shock_targets(std::list<Object*> *hostiles);
    // Kill an enemy ship a shock bolt reached, crediting this ship (GLGame path).
    void shock_hit_ship(Ship *other);
    void set_black_holes(const std::list<BlackHole*> *bhs);
    WrappedPoint gun() const;
    void mark_last_bullet_trail();
    void mark_last_bullet_kills_invincible();
    void mark_last_bullet_piercing();
    void fire_bullet_from_gun();
    bool kill();

  protected:

    // Stop the looping engine/boost sound. Ships that never thrust (e.g. the
    // roaming mini-station) call this so they don't hold an idle hum channel.
    void mute_engine();

    void lay_mine();
    void respawn(const Grid &grid, bool was_killed = true);
    void init(bool no_friction);
    virtual void reset(bool was_killed = true);
    // Shrapnel counts for the deployable blasts (detonate's particle_count:
    // spawns rand()%count + count/2 bullets). ONE constant per weapon, used by
    // BOTH the authoritative detonation paths and the net client's replay
    // (net_mine_exploded / net_missile_exploded) — the two once drifted (the
    // mine's replay stayed at the old 50 after the real blast was cut to 20),
    // making a client's own mine kill a different asteroid set than the host
    // simulated. Never pass a literal at a detonation call site.
    static const int MINE_SHRAPNEL = 20;
    static const int MISSILE_SHRAPNEL = 25;
    void detonate();
    void detonate(Point const position, Point const velocity, int particle_count = 10);
    void giga_detonate(Point const position);

    Point world_size;

    float heat_rate, retro_heat_rate, cool_rate, boost_heat;

    // Forces
    float thrust_force, reverse_force, rotation_force, boost_force;
    // Attributes
    float width, height, mass;
    int value;
    // States
    bool mining, respawns, first_life, toggled;

    //TODO: encapsulate
    friend class GLStation;
    friend class Enemy;
    friend class GLShip;
    friend class GLEnemy;
    friend class GLGame;
    friend class GLTrail;
    // Screenshot harness: respawns the shot game's ships alive at build
    // time (a fresh game's player 1 opens dead in the countdown).
    friend class ShotScene;

  private:
    void safe_position(const Grid &grid, bool try_current = false);
    void tally_nova_kill(const Point &pos);  // call on every asteroid kill; drops pickup every 100
    void fire_lance_pulse(const Grid &grid); // instantaneous lance ray-march (see weapon/lance.h)
    // Central bookkeeping for an asteroid this ship destroyed (score counters,
    // nova feedback, achievements, lifetime stats). All weapon paths call this.
    void credit_asteroid_kill(Object *object, bool nova_feedback = true);
    // Bookkeeping for a downed ship (kill counters, score, enemies_10). The
    // CALLER gates on kill_stop() returning true: shots absorbed by a
    // shield/invincibility (e.g. a disconnected player's parked ship) must
    // not award anything.
    void credit_ship_kill(Ship *other);
    void record_weapon_fired(Save::WeaponEntry::Kind kind);  // weapons_7 tracking

    // Primary selection history: when a limited primary runs dry it is
    // removed and the selection falls back to the weapon the player was
    // using BEFORE it — not the front of the list (the default gun), which
    // is where the old wrap-forward rule always landed because pickups
    // splice the selection to the back. Identity is kind + gun variant
    // (never a pointer/iterator), so it survives list reshuffles and the
    // netplay roster rebuilds; if the remembered weapon is gone, the
    // exhaustion fallback uses the previous list neighbour instead.
    Save::WeaponEntry::Kind last_primary_kind = Save::WeaponEntry::Kind::Default;
    int  last_primary_index = -1;  // Default-gun variant; -1 for the specials
    bool has_last_primary = false;
    void record_primary_selection();  // call BEFORE switching the selection away
    static Save::WeaponEntry::Kind primary_kind_of(Weapon::Base *w, int *index_out);
    // The one fallback policy for removing a primary (exhausted beam/lance/
    // shock, expired god mode): the remembered selection first, then the
    // previous list neighbour, wrapping forward only from the front.
    list<Weapon::Base *>::iterator fallback_primary(list<Weapon::Base *>::iterator to_remove);
    // Remove the selected primary (it ran dry) and move the selection to
    // fallback_primary(), with the weapon-cycle click so the swap is
    // audible. Shared by the press path (shoot()) and the mid-burst
    // dry-switch in step().
    void drop_exhausted_primary();
    void record_primary_fired();  // weapons_7 kind-detect for the selected primary

    void update_god_mode_music(int time_remaining);
    void stop_god_mode_music();
    Mix_Chunk *boost_sound = NULL, *tic_sound = NULL, *tic_low_sound = NULL, *click_sound = NULL;
    Mix_Chunk *missile_explode_sound = NULL, *shield_hum_sound = NULL, *explode_sound = NULL;
    Mix_Chunk *giga_mine_explode_sound = NULL, *mine_explode_sound = NULL;
    Mix_Chunk *missile_fly_sound = NULL;  // loop for replicated missiles (net client)
    Mix_Chunk *shoot_sound = NULL;
    Mix_Chunk *god_mode_music_sound = NULL, *god_mode_music_warn_sound = NULL;
    // The shield hum is ONE shared refcounted loop, not a channel per ship:
    // every local seat spawns invincible at once in split-screen, and N
    // stacked copies of the same full-volume loop were "way too loud" (4P
    // field bug). First own-cues ship to hum starts the channel, the last
    // to stop halts it; overlapping windows hold it at one hum's loudness.
    unsigned out_order_ = 0;             // see out_order()
    static unsigned out_order_seq_;      // monotonic, stamps out_order_
    bool shield_humming = false;         // this ship's contribution
    static int shield_hum_refs;          // ships currently humming
    static int shield_hum_shared_channel;
    int boost_channel = -1;
    int god_mode_music_channel = -1;
    int god_mode_music_phase = 0;  // 0=off, 1=main, 2=warn

    list<Behaviour *> behaviours;
    list<Weapon::Base *> primary_weapons;
    list<Weapon::Base *> secondary_weapons;
    list<Weapon::Base *>::iterator primary;
    list<Weapon::Base *>::iterator secondary;
    std::list<Object*> *missile_asteroids = nullptr;
    const std::list<BlackHole*> *black_holes = nullptr;
    std::list<Object*> *missile_ships_list = nullptr;
    std::list<Object*> *shock_targets = nullptr;  // enemies/stations for bolt seeking
};

// Lifetime SECONDARIES USED (STATS screen): each secondary weapon calls this
// at its post-ammo-check success point (mine/giga/missile/shield/turret; nova
// counts separately at detonation). A free function beside Ship rather than a
// Weapon::Base method because base.h cannot include ship.h (ship.h includes
// the weapon headers). Gates match the shot counters — genuinely
// locally-piloted ships only, cheat-frozen — and live in ship.cpp.
void stat_secondary_used(Ship *ship);

#endif
