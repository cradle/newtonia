#ifndef GL_GAME_H
#define GL_GAME_H

#include "state.h"
#include "savegame.h"
#include "glship.h"
#include "point.h"
#include "warp_pass.h"
#include "grid.h"
#include "glstarfield.h"
#include "glstation.h"
#include "gl_mini_station.h"
#include "asteroid.h"
#include "black_hole.h"
#include "pickup.h"
#include "extra_life.h"
#include "weapon_pickup.h"
#include "mine_pickup.h"
#include "giga_mine_pickup.h"
#include "missile_pickup.h"
#include "shield_pickup.h"
#include "god_mode_pickup.h"
#include "nova_charge_pickup.h"
#include "beam_pickup.h"
#include "lance_pickup.h"
#include "revive_pickup.h"
#include "shock_pickup.h"
#include "net_signal.h"
#include "view/tap_band.h"
#include <SDL.h>
#include <list>
#include <map>
#include <vector>
#include <string>

using namespace std;

class NetSession;
class NetTransport;
namespace Net { class SnapshotAssembler; struct Reader; }

class GLGame : public State {
public:
  GLGame(SDL_GameController *controller = NULL);
  GLGame(const Save::GameState &save, SDL_GameController *controller = NULL);
  // Online host: adopts the Ready session from the lobby; the remote peer
  // drives player 2 via INPUT messages and receives 10 Hz snapshots.
  GLGame(NetSession *session, SDL_GameController *controller);
  // Online client: bootstrapped by the lobby from the first complete
  // snapshot (the save-restore constructor rebuilds the world; the lobby
  // then feeds the snapshot's NetExtras through net_apply_extras). This
  // machine's player is the LAST in the list; player 1 is the remote host.
  GLGame(const Save::GameState &snapshot, NetSession *session,
         SDL_GameController *controller);
  GLGame(GLGame const &other);
  virtual ~GLGame();

  void draw() override;
  void tick(int delta) override;
  void keyboard(unsigned char key, int x, int y) override;
  void keyboard_up(unsigned char key, int x, int y) override;
  void controller(SDL_Event event) override;
  void touch_tap(float nx, float ny) override;
  void touch_joystick(float nx, float ny);
  // The ship this machine's player controls: the first one, except on a
  // net client where the first ship is the remote host's and the local
  // player is the last. Touch input and the touch OSD key off this.
  GLShip *local_player() const;
  // The peer's ship on this machine (the entry local_player() is not), or
  // NULL in single-machine play. The spectate camera target.
  GLShip *remote_player() const;
  // The ship the camera follows: normally the local player, but the peer
  // once this machine's player is fully out and spectating has begun.
  GLShip *camera_target() const;

  // Spectator flow (netplay co-op). When the local player runs out of lives
  // while the peer is still in it, a 5 s "SPECTATING IN N" countdown runs on
  // the local wreck, then the camera hands off to the peer and "SPECTATING"
  // shows at the bottom. The shared GAME OVER card takes over once the peer
  // is also out. Camera rotation stays whatever the viewer selected — the
  // peer's rotate/fixed preference is not adopted.
  bool is_spectating() const;      // countdown elapsed, camera on the peer
  bool spectate_arming() const;    // out, countdown still running
  int  spectate_countdown_secs() const;  // N in "SPECTATING IN N" (5..1)

  friend class Overlay;
  // The between-level intro state adopts the game while it runs (drawing the
  // frozen world's starfield/objects) and hands it back on dismissal.
  friend class Intro;

  bool cleared() const;

  void focus_lost();
  void focus_gained();
  bool back_pressed() override;
  void controller_added(SDL_GameController *ctrl);
  void controller_removed(SDL_JoystickID id);

  list<Asteroid*> *objects;      // alive asteroids (in collision grid)
  list<Asteroid*> *dead_objects; // killed asteroids with lingering debris
  list<Pickup*> *pickups;
  list<BlackHole*> *black_holes;

  int num_x_viewports() const;
  int num_y_viewports() const;
  // Two local players share this machine's screen; online each machine
  // draws only its own full-screen view even though players->size() == 2.
  bool split_screen() const { return net_mode_ == NetOff && players->size() > 1; }
  // Online game in progress (host or client) — the web build keeps a
  // hidden tab ticking only for these (see web_background_tick).
  bool net_active() const { return net_mode_ != NetOff; }
  bool is_visible_to_any_player(const Ship &ship) const;
  bool is_visible_to_any_player(Point p) const;
  float sound_volume_for_point(Point p) const;
  // Online: attenuation with the LOCAL player as the only listener.
  float net_listener_volume(Point p) const;
  bool is_point_faced_by_any_player(Point p) const;
  bool has_free_controller() const;
  // Force-release all players' held controls. Called by states that swallow
  // input (pause, Intro) so a release delivered while the game wasn't
  // listening can't leave a control latched on (e.g. thrust stuck ON).
  void release_player_controls();
private:
  void add_asteroids();
  void add_player2(SDL_GameController *ctrl);
  // include_asteroids=false skips capturing the asteroid list (the delta
  // path diffs asteroids itself and would otherwise discard the capture).
  Save::GameState build_save_data(bool include_asteroids = true) const;
  void save_progress();   // save only when at least one player is alive or has lives
  void toggle_pause(bool broadcast = true);  // broadcast=false: applying a
                                             // peer's PAUSE/RESUME event
  void host_toggle_friendly_fire();  // G key / HUD-text tap; announces the
                                     // room rule online (EV_FRIENDLY_FIRE)
  // The "friendly fire on/off" HUD line doubles as the touch toggle
  // region — one definition for the drawn text (Overlay) and the tap
  // hit-test (touch_tap); see view/tap_band.h.
  TapBand ff_toggle_band() const;
  // Co-op revive (revive_pickup.h): put a fully-out partner back on their
  // last life. Called from the pickup collection site and the
  // NEWTONIA_NET_TEST_REVIVE_MS e2e hook.
  void revive_fallen_partner(Ship *except);
  // Lance ship/station hits: the pulse's ray-march (Ship) only sees
  // asteroids, so the traced polyline comes back here — from the firer's
  // lance_hit_pending (offline/host) or the client's MSG_LANCE — and is
  // tested against ships. Self-hits count only from the first mirror
  // bounce (kills pass through collinearly, so the first direction change
  // IS the first reflection); the partner needs friendly_fire; enemies and
  // the mini-station die; the station hull takes multi-hit damage.
  void resolve_lance_ship_hits(Ship *firer, const std::vector<Point> &pts);
  void draw_map() const;
  // cull_r > 0 skips asteroids further than cull_r + radius from
  // (cam_x, cam_y) — the camera centre in the calling tile's object space —
  // before any geometry is built (see AsteroidDrawer::draw_batch).
  void draw_objects(float direction = 0.0f, bool minimap = false,
                    float cam_x = 0.0f, float cam_y = 0.0f,
                    float cull_r = 0.0f) const;
  void draw_world(GLShip *glship = NULL, bool primary = true) const;
  void draw_perspective(GLShip *glship) const;
  void setup_viewport(bool primary) const;

  // When a generation introduces a new object type, hand this state to a
  // freshly-created Intro state (intro.h/cpp) that shows the object
  // spinning centre-screen until a player presses shoot.
  void maybe_start_intro();

  // ---- netplay (see NETPLAY.md) ----
  // All no-ops when net_mode_ == NetOff. Online, every local-save path is
  // hard-gated off so online play can never clobber the solo save.
  enum NetMode { NetOff, NetHost, NetClient };
  void add_remote_player();       // player 2 without local input bindings
  void net_host_poll();           // apply queued INPUT messages
  // Elastic asteroid-asteroid physics, shared by the host sim
  // (announce=true: ting + EV_ROID_BOUNCE) and the net client's silent
  // per-step mirror — unmirrored, every bounce was a surprise the
  // authoritative records corrected 100 ms later (client-side jitter).
  void elastic_asteroid_collisions(bool announce);
  // Quantum observation flips (4x speed), shared by the host sim and the
  // net client's per-step mirror — see the definition for why.
  void update_quantum_observation();
  void net_host_send_snapshot(int delta);  // 10 Hz world broadcast

  // Client side: visual/kinematic tick (no kills/drops/generation logic),
  // snapshot consumption, prediction correction and INPUT sending.
  void tick_net_client(int delta);
  void net_client_poll();
  void net_client_send_input();
  void net_apply_state(const Save::GameState &s);
  void net_apply_extras(Save::Stream &in, const Save::GameState &s);
  // Delta protocol (M2-6): the ship half of the extras is shared between
  // keyframes and deltas; asteroids arrive as new/dynamic/removed records.
  // apply=false / membership_only=true drive the stale-delta walk: parse
  // past the stale poses, keep only the once-sent new/removed records.
  bool net_apply_ship_extras(Save::Stream &in, const Save::GameState &s,
                             bool apply = true);
  void net_apply_keyframe_asteroid_ids(Save::Stream &in,
                                       const Save::GameState &s);
  void net_apply_delta_asteroids(Save::Stream &in,
                                 bool membership_only = false);
  bool net_send_delta();          // false: too big / not possible -> keyframe

  // The lobby bootstraps the client game (constructor + first snapshot's
  // NetExtras) before handing over the state.
  friend class NetLobby;

  NetMode net_mode_ = NetOff;
  NetSession *net_session_ = nullptr;  // owned when net_mode_ != NetOff
  int net_snapshot_timer_ = 0;
  uint32_t net_snapshot_id_ = 0;
  uint32_t net_last_input_seq_ = 0;
  bool net_have_input_ = false;   // first INPUT initialises the counters
  uint8_t net_prev_boost_ = 0, net_prev_next_weapon_ = 0,
          net_prev_next_secondary_ = 0, net_prev_teleport_ = 0,
          net_prev_respawn_ = 0, net_prev_shoot_press_ = 0,
          net_prev_secondary_press_ = 0;
  uint32_t net_input_seq_ = 0;    // client: outgoing INPUT sequence
  // Client: INPUT mirroring onto the RELIABLE channel — on any held-bit /
  // one-shot change, plus a ~10 Hz refresh. An unreliable-channel blackout
  // otherwise leaves the host acting on stale held bits for up to the 1 s
  // dead-man: phantom thrust that syncs the player ahead of themselves.
  // Same bytes, same seq — the host's seq filter dedupes the twin.
  uint16_t net_mirror_held_ = 0;
  uint8_t net_mirror_counts_ = 0;
  int net_mirror_steps_ = 0;
  uint8_t net_prev_warp_ = 0;     // client: last seen local-ship warp count
  bool net_have_warp_ = false;    // first snapshot baselines the count
  // Client: last net_apply_state was a generation rollover — suppress the
  // vanished-projectile explosion cues for that apply (it's a rebuild).
  bool net_world_rebuilt_last_apply_ = false;
  // Host: held INPUT bits ignored until the client releases the key once.
  // Set to all-ones at each level transition so the remote player starts
  // the new level with controls cleared — exactly like the local player,
  // whose respawn reset() also drops held keys until re-pressed.
  uint16_t net_held_suppress_ = 0;
  Net::SnapshotAssembler *net_assembler_ = nullptr;  // client chunk reassembly
  bool net_ids_adopted_ = false;  // client: bootstrap id adoption ran
  // Client: last applied MSG_DELTA snap id. The rel channel is ordered so
  // this should never fire — pure insurance against a stale/duplicated
  // apply ever reading as "replaying out-of-order states".
  uint32_t net_last_delta_id_ = 0;
  // Client: estimate of the host's game clock "now" (advanced by local
  // running time, reset by every accepted state apply). A post-stall
  // backlog trickles in over several frames, each item describing a PAST
  // the client already extrapolated beyond — applying them toured every
  // asteroid backward then forward at once. Items older than this
  // estimate are dropped instead.
  int net_host_est_ = -1;
  // The estimate is DERIVED each tick as anchor_host + local time since
  // the anchor (paused time excluded). The old "+= delta" accumulator
  // double-counted any tick that both accepted an apply and covered a
  // long frame: the anchor already contains the host's advance over a
  // client-side hitch, so adding the hitch's delta on top left the
  // estimate permanently ahead — every delta then looked ~hitch ms
  // stale, nothing was ever accepted again, and the gate wedged shut
  // (Glenn: continuous "dropped stale delta (~200 ms behind)").
  int net_est_anchor_host_ = -1;   // host clock at the last accepted apply
  int net_est_anchor_local_ = -1;  // our current_time at that moment
  int net_stale_streak_ = 0;       // consecutive stale drops (see hatch)

  // Diagnosis telemetry, no gameplay effect. Host: input-gap forensics —
  // when a gap ends, a 1.5 s observation window counts what arrives so
  // the log alone says whether the client stopped sending (no seqs
  // skipped, normal-rate accepts), packets were lost (seqs skipped, no
  // stragglers), or a queued backlog replayed (accept burst + stale rx).
  int net_input_stale_drops_ = 0;  // seq<=last arrivals since last accept
  int net_gap_deadline_ = 0;       // current_time when the window closes
  uint32_t net_gap_skipped_ = 0;   // seqs missing when the gap ended
  int net_gap_stragglers_ = 0;     // stale-seq arrivals inside the window
  int net_gap_accepts_ = 0;        // accepted INPUTs inside the window
  uint32_t net_gap_max_leap_ = 0;  // biggest forward seq jump in the window
  int net_last_send_time_ = 0;     // client: INPUT send cadence (stall log)
  // Mid-gap markers: fire ONCE when inbound silence crosses 300 ms, with
  // the transport's tx-buffered depth at that instant — the 1 Hz sample
  // can miss a sub-second gap entirely, this cannot. Both roles.
  bool net_quiet_logged_ = false;
  // Hitch breakdown: SDL_GetTicks at tick_net_client entry, so the
  // "slow tick" line can split poll (applies) from step-loop time.
  uint32_t net_tick_t0_ = 0;
  // Client hit-authority (PROTO 13): ids killed locally on our own hit,
  // mapped to a suppression expiry — a keyframe cut before the host
  // processes the claim still lists the id, and re-creating it would
  // resurrect the rock for ~RTT.
  std::map<uint32_t, int> net_predicted_kills_;
  // PROTO 16 twin for enemies (net_ship_id space is separate from the
  // asteroid ids): applied where the extras re-stamp the rebuilt
  // replicas' ids each apply.
  std::map<uint32_t, int> net_predicted_ship_kills_;
  // RX watchdog (both roles): last current_time anything arrived from the
  // peer. A one-way path death (Deck wifi sleep) otherwise leaves a ghost
  // world extrapolating for the ~45 s the transport takes to give up —
  // 10 s of silence while running IS the diagnosis (10 Hz deltas / 125 Hz
  // inputs never pause without the game pausing too).
  int net_last_rx_time_ = 0;

  // Phase 8 polish (see NETPLAY.md)
  static void net_clear_event_outboxes();  // reset the static host outboxes
  void net_send_event(uint8_t code, uint32_t arg = 0);
  void net_handle_event(uint8_t code, uint32_t arg);
  void net_spark_asteroid_at(float x, float y);
  void net_set_generation_banner(int gen);
  bool net_connection_lost_ = false;
  bool net_peer_bye_ = false;  // client: the host said BYE — no auto-rejoin
  int net_banner_ms_ = 0;
  std::string net_banner_text_;
  int net_last_input_time_ = 0;     // host: dead-man switch (1 s)
  bool net_input_zeroed_ = false;
  // RTT probe (MSG_PING/PONG, 1 Hz each way): smoothed round-trip in ms,
  // -1 until the first PONG. Shown on the debug overlay; the client's
  // local-ship blend latency-compensates with it.
  float net_rtt_ms_ = -1.0f;
  int net_ping_timer_ = 0;
  // Last 8 raw RTT samples; net_lead_ms() uses their MINIMUM — a spike is
  // relay queueing, not path length, and must never inflate the lead.
  float net_rtt_ring_[8];
  int net_rtt_ring_n_ = 0, net_rtt_ring_i_ = 0;
  void net_ping_tick(int delta);
  // Answers PING / consumes PONG; true when the message was one of them.
  bool net_handle_ping_pong(uint8_t msg_type, Net::Reader &r);
  // PROTO 18: parse an MSG_LANCE body and push the pulse onto shooter's
  // lance_pulses (display-only flash + attenuated lance sound); false on
  // a malformed body. Shared by the host and client receive paths.
  bool net_receive_lance_pulse(Net::Reader &r, Ship *shooter);
  // How far ahead (ms) to extrapolate a host pose to compensate its
  // transit staleness: RTT/2, capped, 0 before the first PONG.
  float net_lead_ms() const;
  // Reconcile a freshly-applied authoritative pose with the client's own
  // extrapolation. old_render is the pose the player currently SEES.
  // sim_exact=true (asteroids): the sim pose takes the authority exactly
  // — the client mirrors position-dependent gravity, and simulating from
  // a drained pose fed the error back through the field near the black
  // hole — while drawn continuity lives in the render-only net_pose_err
  // offset the AsteroidDrawer adds. sim_exact=false (remote ship, no
  // position-dependent forces): the pose itself keeps continuity and
  // net_smooth_step drains it toward authority. Returns the
  // pre-correction error distance (diagnostics).
  float net_reconcile_pose(Object &o, const WrappedPoint &old_render,
                           bool sim_exact) const;

  // ---- M2-4 rejoin (host only; see NETPLAY.md) ----
  // The lobby's signal connection moves in here so the room stays open
  // for the whole session; on client loss the host keeps playing solo,
  // parks the remote ship, and offers a fresh transport through the room.
public:
  void net_adopt_signal(NetSignal *signal, const std::string &room_code,
                        const std::vector<std::string> &ice_servers,
                        const std::string &room_token);
private:
  void net_host_rejoin_poll(int delta);
  // M3-1: relay-socket upkeep while the CLIENT is healthy — detects a
  // dropped signal socket, reclaims the room with the token, and
  // fast-detects client loss from the room's peer-join notification.
  void net_host_signal_maintain(int delta);
  // Shared plumbing between the two loops above: the M3-1 reclaim
  // countdown, and the signal events both must treat identically.
  void net_host_signal_reclaim_tick(int delta);
  enum NetSignalEventResult {
    NetSigUnhandled,  // not a common event — the caller's loop handles it
    NetSigHandled,    // consumed; keep polling
    NetSigDropped,    // net_signal_ was deleted; the caller must return
  };
  NetSignalEventResult net_host_signal_common_event(const NetSignal::Event &ev);
  NetSignal *net_signal_ = nullptr;     // owned; null in the manual flow
  std::string net_room_code_;
  std::string net_room_token_;        // proves room ownership on reclaim
  bool net_invite_advertised_ = false; // host: "connect" key live for rejoin

  int net_signal_retry_ms_ = 0;       // >0: reclaim attempt countdown
  int net_client_rejoin_ms_ = 0;      // client: auto-rejoin countdown
  std::vector<std::string> net_ice_;  // TURN triples for rejoin re-hosts
  NetTransport *net_rehost_ = nullptr;  // owned until handed to a session
  bool net_rehost_offer_sent_ = false;
  long net_bytes_sent_ = 0;             // M2-6 bandwidth telemetry window

  // M2-6 delta snapshots: what the client is known to have (reliable
  // ordered channel = no acks needed). Reset at every keyframe.
  struct NetAstBase {
    float px, py, vx, vy;
    uint8_t health, state;
    int t;  // current_time when last sent, for the drift check
  };
  std::map<uint32_t, NetAstBase> net_known_;
  int net_slot_ = 0;                    // snapshot slot; every 10th = keyframe
  bool net_force_keyframe_ = true;      // first send / rejoin / new level

  // Re-report the platform online status ("Level N" / "Level N Co-Op").
  // Called wherever the level or player count changes; duplicates are
  // deduped in the Presence layer.
  void update_presence() const;

  static const int step_size = 8;

  Point world;

  int generation;
  int last_tick, time_until_next_step, num_frames, current_time, time_between_steps;
  Uint32 last_draw_time_;
  int time_until_next_generation;
  // game_over: latches once the game has ended for us — all players out, or
  // (netplay) a spectator who then lost the peer. Gates the one-time
  // high-score save, the savegame delete, the in-progress autosave, netplay
  // auto-rejoin suppression, and the spectate flow. NOT a savegame flag:
  // online co-op never writes a resumable save (those paths are NetOff-only).
  bool running, level_cleared, friendly_fire, debug_grid, game_over;
  // High score/stats banked for this game (master's achievements work):
  // save_progress() becomes a no-op once set at game over.
  bool score_saved = false;
  bool auto_paused = false;
  bool save_written_this_death_ = false;
  bool save_deleted_ = false;
  // Lightweight frame telemetry for on-device perf hunts (Android logcat):
  // tick()/draw() bracket themselves; once a second, if the frame rate ran
  // below ~55 fps, one SDL_Log line breaks the frame down (sim vs GL
  // submission vs the rest = swap/present) with the live object counts.
  // Silent at healthy frame rates; no env plumbing needed on device.
  Uint32 perf_window_start_ = 0;
  Uint32 perf_tick_ms_ = 0, perf_draw_ms_ = 0;
  Uint32 perf_tick_max_ = 0, perf_draw_max_ = 0;
  // Lens/warp share of draw (mutable: accumulated inside const draw paths).
  mutable Uint32 perf_lens_ms_ = 0, perf_lens_max_ = 0;
  // Finer draw sub-phases in raw SDL_GetPerformanceCounter ticks (ms
  // resolution is too coarse for these): game objects, starfield, HUD.
  mutable Uint64 perf_objs_pc_ = 0, perf_stars_pc_ = 0, perf_osd_pc_ = 0;
  int perf_frames_ = 0;
  void perf_report();
  int game_over_time;
  // current_time at which the local player went fully out while the peer
  // played on (arms the spectate countdown), or -1 when not spectating.
  int spectate_death_time_ = -1;
  static const int kSpectateDelayMs = 5000;
  void update_spectate();

  static const int default_world_width, default_world_height;
  static const int default_num_asteroids, extra_num_asteroids;
  static const float extra_life_drop_chance;
  static const float weapon_pickup_drop_chance;
  static const float mine_pickup_drop_chance;
  static const float giga_mine_pickup_drop_chance;
  static const float missile_pickup_drop_chance;
  static const float shield_pickup_drop_chance;
  static const float god_mode_pickup_drop_chance;
  static const float beam_pickup_drop_chance;
  static const float lance_pickup_drop_chance;
  static const float shock_pickup_drop_chance;
  // Co-op revive: 10% per asteroid kill while a
  // partner is fully out, at most one in the world at a time.
  static const float revive_pickup_drop_chance;
  mutable WarpPass *warp_pass_;

  Mix_Chunk *tic_sound = NULL;
  Mix_Chunk *pickup_sound = NULL;
  Mix_Chunk *warp_sound = NULL;
  Mix_Chunk *station_explode_sound = NULL;
  Mix_Chunk *pause_music_sound = NULL;
  int pause_music_channel = -1;  // looping pause tune; halted on unpause

  Grid grid;
  GLStarfield *starfield;
  GLStation *station;
  // Station death is detected as an alive→dead transition once per tick
  // (rather than at each damage site) so every kill path — bullets,
  // missiles, ramming, lance resolution, net kill claims — produces the
  // same one-shot boom sound + EV_STATION_BOOM.
  bool station_alive_prev = false;
  GLMiniStation *mini_station;
  list<GLShip*> *enemies, *players;
  list<Object*> *ship_objects;  // Ship* (as Object*) for missile homing
  list<Object*> *shock_targets; // enemies + stations (as Object*) for shock-bolt seeking
  bool all_weapons_cheat = false;  // NEWTONIA_ALL_WEAPONS: grant full arsenal each life
};

#endif
