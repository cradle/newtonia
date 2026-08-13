#ifndef GL_GAME_H
#define GL_GAME_H

#include "state.h"
#include "menu_select.h"
#include "savegame.h"
#include "net_lan.h"
#include "glship.h"
#include "point.h"
#include "warp_pass.h"
#include "grid.h"
#include "glstarfield.h"
#include "glstation.h"
#include "gl_mini_station.h"
#include "asteroid.h"
#include "black_hole.h"
#include "hazard.h"
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
#include "time_slow_pickup.h"
#include "net_identity.h"
#include "net_signal.h"
#include "view/tap_band.h"
#include <SDL.h>
#include <list>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <utility>

using namespace std;

class NetSession;
class NetTransport;
class NetBoard;
namespace Net { class SnapshotAssembler; struct Reader; }
namespace Replay { class Recorder; class Reader; }

class GLGame : public State {
public:
  GLGame(SDL_GameController *controller = NULL, bool allow_dev_players = true);
  GLGame(const Save::GameState &save, SDL_GameController *controller = NULL);
  // Online host: adopts the Ready session from the lobby; the remote peer
  // drives player 2 via INPUT messages and receives 10 Hz snapshots.
  GLGame(NetSession *session, SDL_GameController *controller);
  // B4b: one waiting-room joiner at hand-off — the session (Ready, its
  // WELCOME seat fixed at construction), the worker joiner id its signal
  // frames are stamped with ("" through the LAN/manual doors), and the
  // worker's attestation of it (empty fields = claim-only).
  struct NetSeated {
    NetSession *session;
    std::string jid;
    NetIdentity attested;
  };
  // Online host, waiting-room form: adopts EVERY seated session; the
  // single-session ctor above delegates here with one entry.
  GLGame(const std::vector<NetSeated> &seated, SDL_GameController *controller);
  // Online client: bootstrapped by the lobby from the first complete
  // snapshot (the save-restore constructor rebuilds the world; the lobby
  // then feeds the snapshot's NetExtras through net_apply_extras). This
  // machine's player is the LAST in the list; player 1 is the remote host.
  GLGame(const Save::GameState &snapshot, NetSession *session,
         SDL_GameController *controller);
  // Host process-death resume (NETPLAY.md): rebuild the hosted world from
  // the online save slot (Save::online_load_game) and boot straight into
  // the mid-game client-loss state — reclaim the room with the persisted
  // NetResume ticket and hold both rejoin doors open for the client's
  // auto-rejoin to meet in the middle. Entered from the menu's RESUME
  // HOSTING row, not the lobby.
  GLGame(const Save::GameState &save, const std::string &room_code,
         const std::string &room_token, SDL_GameController *controller);
  // Replay playback (REPLAY.md R2): world bootstrapped from the file's
  // first keyframe — the same restore a joining net client gets — then
  // records drive it through the client apply path. Takes ownership of the
  // reader. Use start_replay_playback(), which parses the bootstrap.
  GLGame(const Save::GameState &snapshot, Replay::Reader *reader);
  // Opens and validates a replay file, returns the playback state — NULL
  // (with a log line) on unreadable/older-version/keyframe-less files.
  static GLGame *start_replay_playback(const std::string &path);
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
  // PROTO 25 seat lookups (FOURPLAYER.md PB-D3): the ship sitting in a
  // 1-based wire seat (NULL if unoccupied), and the seat this machine's
  // own pilot occupies (host/offline 1, client its WELCOME assignment).
  GLShip *player_by_seat(int seat) const;
  int net_local_seat() const;
  Ship *net_firer_ship(int seat) const;
  // The ship the camera follows: normally the local player, but the peer
  // once this machine's player is fully out and spectating has begun.
  GLShip *camera_target() const;
  // B5: the ship the spectate camera follows — the first living OTHER
  // player (respawning ones as fallback), advancing as they fall.
  GLShip *spectate_target() const;

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
  // The screenshot harness (shot_scene.h) composes a shot game to order:
  // clears/spawns objects, parks ships, latches every persistence path off.
  friend class ShotScene;
  // The video harness (video_capture.h) renders a replay to a frame stream:
  // it picks the chrome flags and reads the playback clock to know when the
  // recording has run out.
  friend class VideoCapture;

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
  list<Hazard*> *hazards;        // mid-game obstacles (pulsar/comet/seeker)

  // First hazard of a given kind, or NULL — used by the intro screen to focus
  // on the newly-introduced obstacle.
  Hazard *first_hazard(Hazard::Kind kind) const;

  int num_x_viewports() const;
  int num_y_viewports() const;
  // Two local players share this machine's screen; online each machine
  // draws only its own full-screen view even though players->size() == 2.
  // A 2-player REPLAY renders the same split-screen the game showed while
  // being played — both viewports, each following its own ghost.
  bool split_screen() const {
    return (net_mode_ == NetOff || net_mode_ == NetReplay) &&
           players->size() > 1;
  }
  // Online game in progress (host or client) — the web build keeps a
  // hidden tab ticking only for these (see web_background_tick).
  bool net_active() const { return net_mode_ != NetOff; }
  // The game has ended for everyone: every player dead with no lives left,
  // or the game_over latch (which also covers the terminal spectator case —
  // losing the host while already out). Drives the shared GAME OVER card
  // and the "no pausing a finished game" gate in toggle_pause.
  bool all_players_out() const;
  bool is_visible_to_any_player(const Ship &ship) const;
  bool is_visible_to_any_player(Point p) const;
  float sound_volume_for_point(Point p) const;
  // Online: attenuation with the LOCAL player as the only listener.
  float net_listener_volume(Point p) const;
  // The listener rule for anything happening at a world point, picking
  // between the two above by mode: online only the local camera hears,
  // split-screen every live local player does. Installed as the WorldSound
  // listener for the life of the game, so the shared Asteroid impact
  // chunks — which can see neither a ship nor the GLGame — attenuate too.
  float world_volume(Point p) const;
  // True when every player is on this screen (offline, or a replay's
  // split-screen ghosts) and so every player is a listener.
  bool all_players_local() const;
  bool is_point_faced_by_any_player(Point p) const;
  bool has_free_controller() const;
  // Force-release all players' held controls. Called by states that swallow
  // input (pause, Intro) so a release delivered while the game wasn't
  // listening can't leave a control latched on (e.g. thrust stuck ON).
  void release_player_controls();
private:
  void add_asteroids();
  // Spawn the mid-game hazards this generation calls for (counts scale with
  // generation, like the special-asteroid counts). Appends to `hazards`.
  void add_hazards();
  // Break a small asteroid chunk off a comet at its current position, flung
  // outward from its heading. The chunk is a normal killable asteroid.
  void shed_comet_fragment(const Hazard *comet);
  // Play a bullet-impact sound for a non-fatal hazard hit, rate-limited so a
  // burst of hits can't starve the mixer channel pool.
  void play_hazard_hit_sound(Mix_Chunk *snd);
  // The one local-join path (FOURPLAYER.md D6); gated on LOCAL_PLAYER_CAP.
  void add_local_player(SDL_GameController *ctrl, bool with_keys,
                        bool bypass_cap = false);
  // include_asteroids=false skips capturing the asteroid list (the delta
  // path diffs asteroids itself and would otherwise discard the capture).
  Save::GameState build_save_data(bool include_asteroids = true) const;
  void save_progress();   // save only when at least one player is alive or has lives
  void toggle_pause(bool broadcast = true);  // broadcast=false: applying a
                                             // peer's PAUSE/RESUME event
  // Pause-screen menu, drawn by Overlay::paused. Every pause opens on
  // RESUME — never on the exit row left armed by the last one. PLAYERS
  // (the seat roster) only exists offline, so the row list is 3 or 2 long
  // and pause_row_at maps a selection index onto the right action.
  enum PauseRow { PAUSE_RESUME = 0, PAUSE_PLAYERS = 1, PAUSE_EXIT = 2,
                  PAUSE_ROWS = 3 };
  int pause_selection_ = PAUSE_RESUME;
  int pause_row_count() const { return roster_available() ? 3 : 2; }
  PauseRow pause_row_at(int sel) const {
    if (roster_available()) return (PauseRow)sel;
    return sel <= 0 ? PAUSE_RESUME : PAUSE_EXIT;
  }

  // ---- Seat input roster (offline) -----------------------------------
  // A seat's input is a keyboard cluster (a PlayerKeys slot), a pad, or
  // nothing — and any of them can move between seats. Seat index and keymap
  // slot were hardwired together (seat N always got slot N), which is the
  // only reason P3/P4 couldn't use a keyboard; the roster unpicks that, so
  // 2 keyboard + 2 pad (or any mix) is arrangeable in-game instead of being
  // decided by join order (FOURPLAYER.md O7).
  struct SeatInput {
    enum Kind { None = 0, Keys, Pad };
    Kind kind = None;
    int slot = -1;              // Keys: index into g_prefs.player_keys
    SDL_JoystickID pad = -1;    // Pad: instance id
    bool same_as(const SeatInput &o) const {
      return kind == o.kind && slot == o.slot && pad == o.pad;
    }
  };
  bool roster_available() const;   // offline OR host, non-touch
  // Online-host rows: a remote pilot (KICK / BAN) rather than a local seat.
  // (roster_peer_at is declared with the other NetPeer members, below.)
  bool roster_row_is_peer(int row) const;
  // ...and whether BAN is on offer there (a worker-attested name; see
  // net_identity_anonymous). False leaves KICK as the row's only action.
  bool roster_row_can_ban(int row) const;
  // The trailing ALLOW ANONYMOUS PLAYERS row (host only) — the admission
  // policy the empty seats are filled under. Its twin is the lobby waiting
  // room's row of the same name; both write Preferences::allow_anonymous.
  bool roster_has_anon_row() const;
  bool roster_row_is_anon(int row) const;
  // Removing needs a deliberate second press — one stray confirm should not
  // end someone's game. -1 = nothing armed; otherwise the armed row.
  int roster_kick_armed_ = -1;
  // Which removal the highlighted peer row is offering: false = KICK (they
  // may rejoin), true = BAN (they may not). Left/right picks it, and it
  // resets to the softer one whenever the highlight moves.
  bool roster_ban_ = false;
  bool roster_open() const { return roster_active_ && !running &&
                                    roster_available(); }
  // Rows: one per seat, plus a trailing ADD row while under MAX_PLAYERS.
  int roster_row_count() const;
  void roster_nav(unsigned char key);
  // An unassigned pad takes the highlighted seat by pressing any button —
  // press-to-claim, the couch-co-op idiom. True when it was consumed.
  bool roster_claim_pad(SDL_JoystickID which);
  SeatInput roster_seat_input(int seat) const;
  std::string roster_seat_label(int seat) const;
  // Every input this machine can offer a seat, in cycle order.
  std::vector<SeatInput> roster_input_options() const;
  // Move an input onto a row, taking it off whatever seat held it; the ADD
  // row seats a new player first.
  void roster_apply(int row, const SeatInput &in);
  bool roster_active_ = false;
  int roster_selection_ = 0;
  // True when the pause menu is on screen AND owns navigation input. Touch
  // is excluded: it draws no cursor anywhere and already has both actions
  // (the pause button resumes, the RETURN TO MENU band leaves).
  bool pause_menu_active() const;
  // A replay offers RETURN TO MENU from two different cards — see the
  // definition in glgame.cpp.
  bool replay_exit_offered() const;
  // RESUME / RETURN TO MENU ladder in logical keys, shared by the keyboard
  // and pad paths — the Menu::nav_input pattern, one decision path per
  // screen.
  void pause_nav(unsigned char key);
  // Is this pad already bound to a player? The pause menu only answers to
  // pads that are playing, so an unknown pad's A still joins player 2.
  bool is_player_controller(SDL_JoystickID which) const;
  bool pad_may_command(SDL_JoystickID which) const;
  // Every screen whose only move is "leave" — the GAME OVER card, the
  // terminal disconnect card, a finished replay — draws the shared
  // RETURN TO MENU row, so all of them answer like a menu: confirm
  // (fire/Enter/A/Start/RT) or back (Esc/B/Back), and nothing else. Taking
  // ANY key, as they each used to, meant a stray press ate the score
  // screen. Takes a key already through nav_key/nav_key_from_controller.
  static bool is_exit_key(unsigned char nav) {
    return MenuSelect::is_confirm(nav) || MenuSelect::is_back(nav);
  }
  // The 3 s guard on every game-over exit — a mid-fight fire/confirm still
  // travelling when the game ends must not skip the score screen. One
  // helper because the exits are many (key, pad button, right trigger,
  // touch, the connection-lost card) and the window must not drift
  // between them.
  bool game_over_grace_active() const {
    return game_over_time >= 0 && current_time - game_over_time < 3000;
  }
  void host_toggle_friendly_fire();  // G key / HUD-text tap; announces the
                                     // room rule online (EV_FRIENDLY_FIRE)
  // The "friendly fire on/off" HUD line doubles as the touch toggle
  // region — one definition for the drawn text (Overlay) and the tap
  // hit-test (touch_tap); see view/tap_band.h.
  TapBand ff_toggle_band() const;
  // The in-game touch exit strip (pause / game over / spectator-out) —
  // one definition for the drawn label (Overlay) and the tap hit-test
  // (touch_tap). Landscape reuses the shared TapBand::return_to_menu;
  // portrait re-anchors it near the true bottom, clear of the touch
  // controls (see the definition for the geometry story).
  TapBand exit_band() const;
  // Whether that strip is on screen right now (touch only): GAME OVER, the
  // pause screen, online a fully-out local ship while the peer plays on, or
  // the connection-lost card. ONE rule shared by the band's draw site
  // (Overlay) and the badge rows' hoist decision (Overlay::net_badges) —
  // the unhoisted local row would print through the band's label.
  bool exit_band_showing() const;
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
  // Host-side resolution of a CLIENT weapon polyline's blocked endpoint
  // (MSG_LANCE / MSG_SHOCK): the client stops its lance/shock at an
  // asteroid its local rules say survives, without claiming — the actual
  // outcome (teleport evade, tough chip, ghost/invincible feedback) is
  // the host's call, made here via the survivor's own kill().
  void net_resolve_polyline_block(const std::vector<Point> &pts);
  void draw_map() const;
  // cull_r > 0 skips asteroids further than cull_r + radius from
  // (cam_x, cam_y) — the camera centre in the calling tile's object space —
  // before any geometry is built (see AsteroidDrawer::draw_batch).
  void draw_objects(float direction = 0.0f, bool minimap = false,
                    float cam_x = 0.0f, float cam_y = 0.0f,
                    float cull_r = 0.0f) const;
  void draw_world(GLShip *glship = NULL, int vp_index = 0) const;
  void draw_perspective(GLShip *glship) const;
  // One viewport's pixel rectangle (GL origin: bottom-left). THE geometry
  // definition for the split layout (FOURPLAYER.md D4): 1 player full
  // window, 2 players the orientation-following strip split, 3-4 players a
  // 2x2 grid (P1 top-left, P2 top-right, P3 bottom-left, P4 bottom-right;
  // at 3 players the free bottom-right cell hosts the minimap). draw,
  // dividers and hit-tests must all read it, never re-derive it.
  struct ViewportRect { int x, y, w, h; };
  ViewportRect viewport_rect(int vp_index) const;
  void setup_viewport(int vp_index) const;

  // When a generation introduces a new object type, hand this state to a
  // freshly-created Intro state (intro.h/cpp) that shows the object
  // spinning centre-screen until a player presses shoot.
  void maybe_start_intro();

  // ---- netplay (see NETPLAY.md) ----
  // All no-ops when net_mode_ == NetOff. Online, every local-save path is
  // hard-gated off so online play can never clobber the solo save.
  // NetReplay (REPLAY.md R2) rides the NetClient apply/extrapolate path fed
  // from a file instead of a transport: no session, no INPUT, every ship a
  // ghost. It is a DISTINCT mode value on purpose — net_apply_state's
  // NetClient-gated achievement blocks must stay cold while watching.
  enum NetMode { NetOff, NetHost, NetClient, NetReplay };
  // A remote pilot's hull, without local input bindings. seat 0 = next
  // positional seat (players->size()+1, the 2P-era callers); a waiting-room
  // hand-off passes each peer's WELCOME seat, which can be non-contiguous
  // when a seated joiner left before START freed a lower seat.
  void add_remote_player(uint8_t seat = 0);
  void net_host_poll();           // apply queued INPUT messages
  // Elastic asteroid-asteroid physics, shared by the host sim
  // (announce=true: ting + EV_ROID_BOUNCE) and the net client's silent
  // per-step mirror — unmirrored, every bounce was a surprise the
  // authoritative records corrected 100 ms later (client-side jitter).
  void elastic_asteroid_collisions(bool announce);
  // One elastic pair's separation + impulse + ting (see the .cpp).
  void collide_elastic_pair(Asteroid *a, Asteroid *b, bool announce);
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
  // Build (and tee to the recorder) one delta; send it when can_send.
  // false: too big / not possible -> caller does a keyframe instead.
  bool net_send_delta(bool can_send);

  // REPLAY.md R1 seam: payload builders shared by the online host and the
  // replay recorder (they must never fork). Both move net_known_, the delta
  // baseline — safe, because only one consumer is ever active: offline the
  // recorder's cadence calls them, online ONLY the host send path does and
  // the recorder tees the built bytes (never a second build).
  // counts (optional): out {new, dyn, removed} for telemetry.
  void net_build_keyframe_payload(Save::MemStream &payload);
  bool net_build_delta_payload(Save::MemStream &payload, int counts[3] = NULL);

  // The lobby bootstraps the client game (constructor + first snapshot's
  // NetExtras) before handing over the state.
  friend class NetLobby;

  NetMode net_mode_ = NetOff;
  // ---- One remote peer (Phase B1, FOURPLAYER.md PB-D1) ----
  // The host will hold up to NET_PLAYER_CAP-1 of these (B4); a client holds
  // exactly one — its link to the host. The peer object OUTLIVES its
  // session: a dropped session is deleted while the peer (identity, loss
  // state) survives the rejoin window.
  struct NetPeer {
    NetSession *session = nullptr;  // owned (the session owns its transport)
    // B4: the wire seat this peer's pilot occupies (2..MAX_PLAYERS —
    // WELCOME assigns it, Ship::net_seat mirrors it on the hull) and the
    // signaling worker's joiner id its frames are stamped with (empty on
    // a pre-multi-join worker or the manual/LAN flows). Defaults match
    // the single-peer era: every existing adoption site is seat 2.
    uint8_t seat = 2;
    std::string jid;
    // The peer's badge identity (name + platform, net_identity.h), copied
    // from the session at adoption and REFRESHED on every rejoin handshake
    // — kept here rather than read through the session so the badge
    // survives the sessionless window while a dropped peer rejoins.
    // Default (unknown) for a legacy peer: the overlay then renders
    // exactly the identity-less UI.
    NetIdentity identity;
    // The worker's attestation, kept SEPARATELY from the folded identity
    // above: the rejoin-Ready refresh replaces `identity` wholesale with
    // the CLAIMED wire parse, which would demote an attested badge to the
    // role label — so the refresh re-folds this copy on top. Cleared once
    // per loss (net_host_rejoin_park_peer): the slot may be refilled by
    // a DIFFERENT friend, whose own announce re-attests through the
    // worker (Event::Identity restores it).
    NetIdentity attested;
    // This peer paired through a LOCAL door (LAN beacon) rather than the
    // signaling worker: the offline display carve-out applies PER PEER in
    // a mixed room (net_id_ctx_for_seat) — its claimed name renders, like
    // the classic worker-less flows, while relay-paired peers stay
    // ONLINE-strict. A relay REJOIN re-pairs through the worker and
    // clears it.
    bool offline_paired = false;
    // Transport dead / dead-man tripped. Room-level questions go through
    // the any/all predicates below, which diverge at B4.
    bool lost = false;
    // B5: this peer's once-per-loss park ran (hull frozen, session
    // dropped, attestation cleared). Cleared when its rejoin completes.
    bool parked = false;
    // ---- Host input pipeline for this peer's INPUT stream ----
    uint32_t last_input_seq = 0;
    bool have_input = false;  // first INPUT initialises the counters
    uint8_t prev_boost = 0, prev_next_weapon = 0, prev_next_secondary = 0,
            prev_teleport = 0, prev_respawn = 0, prev_shoot_press = 0,
            prev_secondary_press = 0;
    // Held INPUT bits ignored until this peer releases the key once
    // (all-ones at each level transition, like the local respawn reset).
    uint16_t held_suppress = 0;
    int last_input_time = 0;   // host: dead-man switch (1 s)
    bool input_zeroed = false;
    // Input-gap telemetry (log-only): distinguish freeze vs loss vs
    // backlog replay from the seq pattern inside a 300 ms window.
    int input_stale_drops = 0;  // seq<=last arrivals since last accept
    int gap_deadline = 0;       // current_time when the window closes
    uint32_t gap_skipped = 0;   // seqs missing when the gap ended
    int gap_stragglers = 0;     // stale-seq arrivals inside the window
    int gap_accepts = 0;        // accepted INPUTs inside the window
    uint32_t gap_max_leap = 0;  // biggest forward seq jump in the window
    // Mid-gap marker: fires ONCE when inbound silence crosses 300 ms.
    bool quiet_logged = false;
    // RTT probe (MSG_PING/PONG, 1 Hz each way): smoothed round-trip in
    // ms, -1 until the first PONG. Ring keeps the last 8 raw samples;
    // net_lead_ms() uses their MINIMUM — a spike is relay queueing, not
    // path length, and must never inflate the lead.
    float rtt_ms = -1.0f;
    int ping_timer = 0;
    float rtt_ring[8];
    int rtt_ring_n = 0, rtt_ring_i = 0;
  };
  std::vector<NetPeer *> net_peers_;
  NetPeer *net_peer() const {
    return net_peers_.empty() ? nullptr : net_peers_.front();
  }
  NetPeer &net_peer_make() {
    if (net_peers_.empty()) net_peers_.push_back(new NetPeer());
    return *net_peers_.front();
  }
  // B4b: append a peer (the multi-session host ctor's per-joiner adoption;
  // net_peer_make() only ever names the front slot).
  NetPeer &net_peer_add() {
    net_peers_.push_back(new NetPeer());
    return *net_peers_.back();
  }
  NetSession *net_session() const {
    NetPeer *p = net_peer();
    return p ? p->session : nullptr;
  }
  float net_rtt_ms() const {  // smoothed peer RTT, -1 until the first PONG
    NetPeer *p = net_peer();
    return p ? p->rtt_ms : -1.0f;
  }
  const NetIdentity &net_peer_identity() const {
    static const NetIdentity kNone;
    NetPeer *p = net_peer();
    return p ? p->identity : kNone;
  }
  // Room-level loss predicates. Identical with one peer; at B4 `any` means
  // "a seat is open" (re-host door, invite re-advertise) and `all` means
  // "the room is effectively dead" (terminal card, stop sending).
  bool net_any_peer_lost() const {
    for (NetPeer *p : net_peers_)
      if (p->lost) return true;
    return false;
  }
  bool net_all_peers_lost() const {
    if (net_peers_.empty()) return false;
    for (NetPeer *p : net_peers_)
      if (!p->lost) return false;
    return true;
  }
  // Delete the peer's dead session, keeping the peer (rejoin window).
  // Out-of-line: NetSession is forward-declared here, and deleting an
  // incomplete type would skip its destructor (transport never closed).
  void net_drop_session();
  void net_drop_session(NetPeer &p);  // B5: per-seat form
  // B5 seat lookups over the peer roster (player_by_seat's sibling).
  NetPeer *net_peer_by_seat(int seat) const {
    for (NetPeer *p : net_peers_)
      if ((int)p->seat == seat) return p;
    return nullptr;
  }
  // The rejoin door serves ONE open seat at a time: the lowest lost seat
  // whose session is gone (a lost peer with a session is a door adoption
  // mid-handshake). Serialized on purpose — the relay offer is a single
  // unaddressed slot, so two simultaneous rejoiners would scramble.
  NetPeer *net_door_peer() const {
    NetPeer *door = nullptr;
    for (NetPeer *p : net_peers_)
      if (p->lost && !p->session && (!door || p->seat < door->seat))
        door = p;
    return door;
  }
  NetPeer *net_handshaking_lost_peer() const {
    for (NetPeer *p : net_peers_) {
      // A seat mid-KICK is lost and STILL holds its session — deliberately,
      // so the goodbye reaches the wire before the transport dies
      // (net_kick_closing_). That is not a rejoin in flight, and the door
      // must not read it as one: its session is already Ready, so the very
      // next tick "completed" the adoption — unparking the hull we had just
      // frozen and announcing "PLAYER N RECONNECTED" about the player we
      // had just removed (field, 2026-08-13). The drain runs ahead of the
      // pause gate, so this exclusion always ends.
      if (net_kick_draining((int)p->seat))
        continue;
      if (p->lost && p->session) return p;
    }
    return nullptr;
  }
  // Display context for the peer identity (net_identity.h): a room-code
  // session ran through the signaling worker, so it is ONLINE-strict — a
  // stranger is possible, only ATTESTED fields render. The manual clipboard
  // / LAN fallback is worker-less (OFFLINE) and renders the peer's claimed
  // name. Default ONLINE (strict): the lobby sets it false for the manual
  // path, so forgetting to set it can only under-render, never leak a claim.
  bool net_worker_session_ = true;
  NetIdentityCtx net_id_ctx() const {
    return net_worker_session_ ? NET_ID_ONLINE : NET_ID_OFFLINE;
  }
  // Per-seat badge identity, both roles (post-B7 4P HUD): the host answers
  // from its roster (each NetPeer carries its folded identity); a client
  // answers seat 1 from the handshake identity and other seats from the
  // MSG_PEER_IDENT relay store below. Unknown/own/absent seats return the
  // empty identity, which every renderer already treats as the legacy
  // no-badge case (role-label fallback).
  const NetIdentity &net_identity_for_seat(int seat) const {
    static const NetIdentity kNone;
    if (net_mode_ == NetHost) {
      NetPeer *p = net_peer_by_seat(seat);
      return p ? p->identity : kNone;
    }
    if (net_mode_ == NetClient) {
      if (seat == 1) return net_peer_identity();
      if (seat >= 2 && seat <= MAX_PLAYERS) return net_seat_identities_[seat];
    }
    return kNone;
  }
  // A client's store of the OTHER clients' identities, indexed by wire seat
  // (0 and 1 unused — the host's identity comes from the handshake). Filled
  // by the MSG_PEER_IDENT relay; empty = role label, exactly the legacy UI.
  NetIdentity net_seat_identities_[MAX_PLAYERS + 1];
  // The relay's offline-paired flag per seat (client mirror of
  // NetPeer::offline_paired): that seat paired through the host's LAN
  // door, so its CLAIMED name renders (see net_id_ctx_for_seat).
  bool net_seat_offline_paired_[MAX_PLAYERS + 1] = {};
  // Display context PER SEAT: the session-global rule (net_id_ctx), except
  // a LAN-door-paired peer gets the offline carve-out individually — a
  // mixed relay+LAN room used to render the LAN friend as a bare role
  // label because one worker anywhere made the whole session ONLINE-strict.
  NetIdentityCtx net_id_ctx_for_seat(int seat) const {
    if (net_id_ctx() == NET_ID_OFFLINE) return NET_ID_OFFLINE;
    if (net_mode_ == NetHost) {
      NetPeer *p = net_peer_by_seat(seat);
      if (p && p->offline_paired) return NET_ID_OFFLINE;
    } else if (net_mode_ == NetClient && seat >= 2 && seat <= MAX_PLAYERS &&
               net_seat_offline_paired_[seat]) {
      return NET_ID_OFFLINE;
    }
    return net_id_ctx();
  }
  // Last WELCOME-assigned seat a live session reported (see
  // net_local_seat): survives the sessionless rejoin window. 0 = never
  // had a session (pre-handshake), which reads as the pre-B4 default 2.
  mutable int net_local_seat_cache_ = 0;
  // Host->client seat-identity relay (see net_protocol.h MSG_PEER_IDENT):
  // one message per remote seat, to one peer or to every live session.
  void net_send_seat_identities_to(NetPeer &peer);
  void net_broadcast_seat_identities();
  // Rejoin-by-identity (NetSession::set_seat_resolver): the parked seat
  // whose remembered pilot the claimed HELLO identity matches, or 0.
  int net_rejoin_seat_for_identity(const NetIdentity &claimed) const;
  // Eaten-offer watchdog (see net_host_rejoin_poll): ms the door's offer
  // has sat unanswered with no handshake in flight.
  int net_rehost_offer_age_ms_ = 0;
  // Fallback label when the peer's claim carries no renderable name
  // (badge-only identity — e.g. an iOS host with no Game Center
  // sign-in). The client of a LAN-door session knows the host's
  // beaconed device name — the name it tapped to join, and the rejoin
  // identity — which beats a bare role label ("IPHONE - IOS" instead of
  // "PLAYER 1 - IOS"). Everywhere else the role label stands (the host
  // never learns the client's device name; beacons are host->joiner).
  std::string net_peer_fallback() const {
    if (net_mode_ == NetClient && !net_lan_host_name_.empty())
      return net_lan_host_name_;
    return net_mode_ == NetClient ? "PLAYER 1" : "PLAYER 2";
  }
  // The LOCAL player's own role label (the badge row above the peer's,
  // Overlay::net_badges): the host is player 1, a client its wire seat
  // (B7: a seat-3 client without a platform identity wore "PLAYER 2").
  std::string net_local_fallback() const {
    if (net_mode_ == NetClient)
      return "PLAYER " + std::to_string(net_local_seat());
    return "PLAYER 1";
  }
  // Called by the lobby (a friend) right after construction: fold the
  // worker's peer attestation into the peer's identity and record whether a
  // worker was in the session (see net_worker_session_). Both run AFTER the
  // net constructor composed the JOINED greeting — with the strict default
  // context and a claimed-only identity, so the name could never render —
  // hence each recomposes the banner with the final identity/context.
  void net_set_worker_session(bool worker) {
    net_worker_session_ = worker;
    net_refresh_join_banner();
  }
  // The worker joiner id (B3 `from` stamp) of the peer this session is
  // bound to — set by the lobby at hand-off and by the rehost door when it
  // adopts an answer. Empty against a pre-multi-join worker. In-game
  // Identity events are accepted only when their stamp matches: with the
  // room now admitting extra joiners, an unmatched announce is a THIRD
  // party whose badge must not overwrite the paired peer's.
  std::string net_peer_jid_;
  void net_set_peer_jid(const std::string &jid) {
    net_peer_jid_ = jid;
    if (!net_peers_.empty()) net_peers_.front()->jid = jid;
  }
  void net_apply_peer_attestation(const NetIdentity &attested) {
    NetPeer &p = net_peer_make();
    p.attested = attested;
    net_apply_attested(p.identity, attested);
    net_refresh_join_banner();
  }
  // Recompose the initial JOINED greeting from the current identity and
  // display context. Safe to call any time: it refreshes only while the
  // greeting is still the banner on screen (tracked via
  // net_join_banner_text_), so a late in-game attestation can rename it
  // but can never clobber a LEVEL/RECONNECTED banner.
  void net_refresh_join_banner();
  std::string net_join_banner_text_;  // what the greeting last composed
  int net_snapshot_timer_ = 0;
  uint32_t net_snapshot_id_ = 0;
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
  // Client anti-flicker for limited-ammo primaries (Beam/Lance/Shock/GodMode):
  // the client fires locally and decrements ammo instantly, but the host's
  // 10 Hz snapshot lags ~1 RTT and would restore the pre-fire (higher) count
  // for a beat. net_apply_state suppresses a snapshot INCREASE on the local
  // ship's primaries unless a pickup is legitimately expected — a PER-WEAPON
  // latch, armed by the reliable EV_PICKUP event whose arg names the kind
  // (only when OUR ship collected a primary-ammo pickup) and consumed the
  // moment that weapon's increase lands (or timed out). One shared scalar
  // armed by any pickup mis-fired both ways: a lag blip on weapon A consumed
  // the latch meant for weapon B's real pickup (pinning B's ammo low for the
  // rest of the generation), and an unrelated host-side pickup admitted a
  // blip as if it were real.
  enum NetPrimaryKind { NET_PRIM_NONE = 0, NET_PRIM_BEAM = 1, NET_PRIM_LANCE = 2,
                        NET_PRIM_SHOCK = 3, NET_PRIM_GOD = 4, NET_PRIMARY_KINDS = 5 };
  int net_pickup_latch_[NET_PRIMARY_KINDS] = {0, 0, 0, 0, 0};
  // Per-poll budget for the EVENT codes that do real work (EV_RAM_BLAST
  // spawns bullets, EV_ACHIEVEMENT pokes the platform SDK). EVENT itself is
  // never drop-gated by the flood budgets — it is reliable+ordered and
  // stateful, so a dropped one is gone for good — but its heavy effects
  // bound themselves here instead. Reset at the top of each poll.
  static const int NET_EVENT_EFFECTS_PER_POLL = 8;
  int net_event_effect_budget_ = NET_EVENT_EFFECTS_PER_POLL;

  // Diagnosis telemetry, no gameplay effect. Host: input-gap forensics —
  // when a gap ends, a 1.5 s observation window counts what arrives so
  // the log alone says whether the client stopped sending (no seqs
  // skipped, normal-rate accepts), packets were lost (seqs skipped, no
  // stragglers), or a queued backlog replayed (accept burst + stale rx).
  int net_last_send_time_ = 0;     // client: INPUT send cadence (stall log)
  // Mid-gap markers: fire ONCE when inbound silence crosses 300 ms, with
  // the transport's tx-buffered depth at that instant — the 1 Hz sample
  // can miss a sub-second gap entirely, this cannot. Both roles.
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
  // B4 targeted form: one peer only. Tees to the replay like the broadcast
  // form (N=1 byte-identity); a fan-out that already teed the event on its
  // first send passes tee=false on the rest (the EV_PICKUP loop) so one
  // collection is one record, not one per peer.
  void net_send_event_to(NetPeer &peer, uint8_t code, uint32_t arg = 0,
                         bool tee = true);
  // B7: the peer whose pilot is `s`, or null (an enemy, the host's own
  // ship, offline). The seat-keyed twin of the old `s ==
  // remote_player()->ship` test, which only ever matched the BACK peer —
  // at 3-4P that dropped a non-back seat's earn and, worse, the
  // broadcast reply unlocked on every bystander client (the receive case
  // attributes EV_ACHIEVEMENT to the local ship unconditionally).
  NetPeer *net_peer_for_ship(const Ship *s) const {
    if (net_mode_ != NetHost) return nullptr;
    for (NetPeer *p : net_peers_) {
      GLShip *gs = player_by_seat((int)p->seat);
      if (gs && gs->ship == s) return p;
    }
    return nullptr;
  }
  void net_host_poll_peer(NetPeer &peer);  // one peer's drain (B4)
  // `from` names the peer whose transport delivered the event (host-side
  // drain); null on the client/replay paths, where the sender is the host.
  void net_handle_event(uint8_t code, uint32_t arg, NetPeer *from = nullptr);
  void net_spark_asteroid_at(float x, float y);
  void net_set_generation_banner(int gen);
  bool net_peer_bye_ = false;  // client: the host said BYE — no auto-rejoin
  // client: the host KICKED us. Takes the same terminal, no-auto-rejoin
  // path as a BYE (net_peer_bye_ is set alongside, so every existing
  // no-rejoin test keeps working) and only changes what the card says —
  // "the host left" would be a lie, and the player needs to know why the
  // game ended or they will just try to reconnect.
  bool net_kicked_ = false;
  // True while the connection-lost card owns input: every lost link EXCEPT
  // the host-with-an-open-door notice, where the game plays on. While this
  // holds, keyboard_up/controller answer only the leaderboard prompt
  // (which outranks the card at a lost-link game over) and the card's own
  // exits — so pause_menu_active() refuses too, or the pause menu draws
  // live-looking rows over input the card is swallowing (the host pausing
  // and then leaving handed the client a highlighted RESUME and a second
  // RETURN TO MENU that answered nothing; field, 2026-08-07). One
  // predicate for the input handlers and the overlay — net_overlays keys
  // its host-notice branch off it too — like pause_menu_active itself.
  bool net_card_owns_input() const {
    return net_all_peers_lost() &&
           !(net_mode_ == NetHost && (net_signal_ || net_lan_door_open()));
  }
  // Nav keys pressed (key-DOWN) while the card owns input: keyboard_up
  // exits only on these — board_prompt_pressed_'s pattern — so a fire key
  // held through the disconnect and released into the card can't throw
  // the session away. Stale entries are cleared on the first key-down
  // after the link is healthy again.
  std::set<unsigned char> net_card_pressed_;
  int net_banner_ms_ = 0;
  std::string net_banner_text_;
  // True for the "<NAME> RECONNECTED" notice: drawn up top at the
  // DISCONNECTED header's position/size so the pair share a height; other
  // banners (level, friendly fire, the JOINED greetings) keep the
  // just-above-middle spot.
  bool net_banner_header_ = false;
  // Client: false until the first EV_FRIENDLY_FIRE lands. The first event
  // is the initial room-rule sync on join, adopted silently — a banner
  // there would stomp the "JOINED <NAME> SERVER" greeting and announce a
  // "change" the joiner never saw. Later events are real toggles.
  bool net_ff_synced_ = false;
  void net_ping_tick(int delta);  // B5: probes every live peer
  // Answers PING / consumes PONG; true when the message was one of them.
  // `from` = the peer whose transport delivered it (host drain); null on
  // the client, where the counterparty is the front (host) peer.
  bool net_handle_ping_pong(uint8_t msg_type, Net::Reader &r,
                            NetPeer *from = nullptr);
  // PROTO 18: parse an MSG_LANCE body and push the pulse onto shooter's
  // lance_pulses (display-only flash + attenuated lance sound); false on
  // a malformed body. Shared by the host and client receive paths.
  bool net_receive_lance_pulse(Net::Reader &r, Ship *shooter);
  // PROTO 22: parse a shock-bolt polyline into a display bolt on `shooter`
  // (+ zap sound); `out` (optional) receives the points for host-side
  // station/mini hull resolution.
  bool net_receive_shock_pulse(Net::Reader &r, Ship *shooter,
                               std::vector<Point> *out);
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
  // Re-announce the host's identity to the worker (NETPLAY.md V0/V1) after a
  // room reclaim, so it re-attests and re-broadcasts to the (re)joiner.
  void net_send_local_identity();
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
  // Host process-death resume (NETPLAY.md): checkpoint the session ticket
  // (NetResume) + the online world save so a killed process can offer
  // RESUME HOSTING on relaunch. Rides every auto-save moment plus client
  // join/leave; a game over or deliberate teardown deletes both files.
  void net_host_resume_persist();
  // Refresh cadence for the ticket file alone (~100 bytes) — its age is
  // how a relaunch tells "host died moments ago, reclaim grace open"
  // from a stale leftover. The world save stays on the auto-save moments.
  int net_resume_ticket_ms_ = 0;

  int net_signal_retry_ms_ = 0;       // >0: reclaim attempt countdown
  int net_client_rejoin_ms_ = 0;      // client: auto-rejoin countdown
  std::vector<std::string> net_ice_;  // TURN triples for rejoin re-hosts
  NetTransport *net_rehost_ = nullptr;  // owned until handed to a session
  bool net_rehost_offer_sent_ = false;
  // The current rehost transport's already-drained trickle candidates
  // ("mid\nsdp" as polled): a RE-pushed offer (watchdog, room reclaim)
  // resets the worker's stored candidate set, and the transport streams
  // each candidate exactly once — without a re-send from this cache a
  // TURN-only rejoiner bootstraps candidate-less and eats a full ICE
  // timeout before the next fresh offer. Cleared with each new transport.
  std::vector<std::string> net_rehost_cands_;
  // Which door the in-flight rejoin adoption came through (LAN beacon vs
  // relay): applied to the ADOPTED peer's offline_paired at Ready — the
  // seat resolver can re-map the adoption, so the door guess must not be
  // mutated at answer/beacon time. One adoption at a time (both doors
  // share net_rehost_seat_ / net_handshaking_lost_peer).
  bool net_rehost_adopt_lan_ = false;
  // ---- LAN rejoin (round 4; see NETPLAY.md "LAN is not a mode") ----
  // On client loss the host re-opens the LAN door TOO: a fresh beacon +
  // blob listener beside the relay rejoin offer (or alone, when the
  // session came through the LAN door and there is no signal). The
  // dropped peer rediscovers the host by name and re-pairs; whichever
  // door completes first is adopted. Client side: net_lan_host_name_
  // (set by the lobby on a LAN join) is the rejoin identity the way
  // net_room_code_ is for relay joins — a loss hands the game to a
  // browsing NetLobby that auto-selects that name when it reappears.
  bool net_host_lan_rejoin_poll(int delta);  // false = LAN unavailable
  // Once per loss per SEAT: park the hull + drop the session; pauses the
  // room only when the last live remote went (PB-D7 play-on policy).
  // keep_session: leave the transport alive for the caller to close on its
  // own schedule. A KICK needs this — park's own drop would destroy the
  // channel with the goodbye still queued in it.
  void net_host_rejoin_park_peer(NetPeer &p, bool keep_session = false);
  // Host: remove a peer and free its seat (the roster's KICK action).
  // Remove a peer: ban=false lets them rejoin with the room code, ban=true
  // refuses their next handshake for this room's lifetime. Two actions on
  // the roster row (left/right), one code path.
  void net_kick_peer(NetPeer &p, bool ban);
  // Seats whose kicked session is draining, each with the ms left before it
  // is torn down. The EV_KICKED has to actually reach the wire first:
  // NetSession::update() returns immediately for a Ready session, so there
  // is no "pump then close" — deleting the transport on the next statement
  // can destroy the channel with the message still queued in SCTP, and the
  // peer would see a bare disconnect and rejoin, which is precisely what
  // the event prevents. Keyed by SEAT: a bare lost && parked && session
  // predicate is exactly net_handshaking_lost_peer(), so it would also
  // match another seat's in-flight rejoin and delete that innocent
  // handshake. A LIST, not one slot (NetLobby::closing_ is the waiting
  // room's twin): the roster stays open after a kick precisely so the host
  // can remove two peers back to back, and a second kick inside the first
  // one's 600 ms window used to overwrite the slot — seat one's exclusion
  // above ended early and the rejoin door adopted the peer just kicked.
  std::vector<std::pair<uint8_t, int>> net_kick_closing_;
  bool net_kick_draining(int seat) const {
    for (const std::pair<uint8_t, int> &k : net_kick_closing_)
      if ((int)k.first == seat) return true;
    return false;
  }
  // The peer occupying a roster row, or null (declared here: NetPeer is
  // defined below the roster block).
  NetPeer *roster_peer_at(int row);
  int net_rehost_seat_ = 0;  // the seat the open relay/LAN door serves
  void net_host_rejoin_session_update(int delta);  // shared adopt/resume
  void net_lan_rejoin_reset();
  bool net_lan_door_open() const;
  NetLan::Announce net_lan_announce_;
  NetTransport *net_lan_rehost_ = nullptr;  // owned until adopted
  bool net_lan_offer_set_ = false;
  // B5: the ROOM-level pause latch — set when the last live peer's park
  // paused the game (per-seat park state lives on NetPeer::parked);
  // cleared when any rejoin completes so the next full loss pauses afresh.
  bool net_rejoin_parked_ = false;
  std::string net_lan_host_name_;    // client: LAN host to rediscover
  // Host: the name the lobby's beacon advertised, frozen at hand-off for
  // the whole session — the loss re-beacon repeats it verbatim so a
  // dropped client's rediscover-by-name can't miss on a drifted
  // local_host_name() (the iOS Game Center alias changes it when sign-in
  // resolves). The lobby always sets it; the re-beacon's empty-fallback
  // (fresh read, frozen from then on) is belt-and-suspenders for any
  // future host-game path that skips the lobby.
  std::string net_lan_beacon_name_;
  // Client auto-rejoin handed the flow to a fresh NetLobby: the dtor then
  // leaves the verification credential alone (that lobby warmed its own
  // ticket in its constructor — releasing here would cancel it and ship the
  // rejoin's identity announce credential-less). See ~NetLobby's twin flag.
  bool net_handed_to_lobby_ = false;
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

  // ---- replay recording (REPLAY.md R1) ----
  // Every solo game records into replays/current.nrp via the snapshot-
  // builder seam above; every ONLINE game records into replays/online.nrp
  // (host: tee of the snapshots it builds and sends; client: tee of the
  // stream it receives). Started lazily on the first tick (the net ctors
  // delegate to the offline ctors and set net_mode_ afterwards, so
  // construction can't know the game's mode); NEWTONIA_REPLAY_DISABLE
  // is the escape hatch. Checkpoint flushes: level rollover (the intro
  // screen, when one follows, is the slack window the write lands in),
  // pause, focus loss. finalize+rotation at game over / destruction.
  void replay_start();
  void replay_record_slot(int delta);  // one KEYFRAME/DELTA per 100 ms run
                                       // (offline cadence only — the host
                                       // tees inside net_host_send_snapshot)
  // Drain the Ship::replay_* effect outboxes (lance/shock/ring visuals the
  // snapshots don't carry): recorded as REC_EFFECT when recording, else
  // discarded. Called once per offline/host tick and per client tick; a
  // non-recording game just gets the keep-empty clear.
  void replay_drain_effects();
  // Receive-side effect tees (online recording): the REMOTE player's
  // weapon visuals arrive as MSG_LANCE/MSG_SHOCK/MSG_SHOT, not through the
  // local outboxes — record them at their receive sites.
  void replay_record_polyline(uint8_t subtype, const Ship *shooter,
                              const std::vector<Point> &pts);
  void replay_record_shot(float x, float y, uint8_t kind);
  // Index of a ship in the players list (-1 if absent) — the REC_EFFECT
  // attribution byte, resolved the same way on record and playback.
  int player_index_of(const Ship *s) const;
  void replay_finish(bool ended);      // finalize; deletes replay_
  Replay::Recorder *replay_ = nullptr;
  bool replay_tried_ = false;          // lazy-start ran (or was skipped)
  int replay_slot_timer_ = 0;          // 100 ms cadence accumulator
  // Random id stamped into the savegame (v17) and the replay header so a
  // resume can continue the same recording (run-scoped replays). Set by
  // both offline ctors; carried by saves; 0 never happens for new games.
  uint64_t run_id_ = 0;
  bool replay_resume_candidate_ = false;  // the loaded save carried a run_id

  // ---- replay playback (REPLAY.md R2; net_mode_ == NetReplay) ----
  // Apply records that have come due on the playback clock (the file-fed
  // stand-in for net_client_poll).
  void tick_replay_poll(int delta);
  Replay::Reader *replay_reader_ = nullptr;  // owned
  int replay_clock_ms_ = 0;       // timeline position (slot * 100 domain)
  // Last FX_SHOT sound per ghost, replay-clock domain: the playback
  // mirror of Ship::net_clone_sound_ms's one-sound-per-burst window.
  static const int REPLAY_FX_SHOT_PLAYERS = 4;
  int replay_fx_shot_ms_[REPLAY_FX_SHOT_PLAYERS] = {-1000, -1000, -1000,
                                                    -1000};
  float replay_speed_ = 1.0f;     // 0.25x..4x, =/- keys (never a cheat)
  bool replay_finished_ = false;  // past the last record: world frozen
  uint16_t replay_save_version_ = 0;  // savegame format of the payloads
  // True only while the factory applies the bootstrap keyframe's extras:
  // an alive→dead "transition" there is initial state, not an event — a
  // new game starts dead in the spawn countdown, and detonating painted
  // an explosion no real new game shows.
  bool replay_bootstrap_apply_ = false;

  // ---- leaderboard game-over flow (LEADERBOARD.md L2) ----
  // Armed at game-over finalize when the run just promoted best.nrp (a new
  // personal best): an async qualify against the board worker, and only a
  // would-place answer shows the UPLOAD TO LEADERBOARD? prompt on the GAME
  // OVER card. The card must never block on the network: no answer within
  // BOARD_QUALIFY_TIMEOUT_MS = no prompt, and leaving to the menu abandons
  // everything harmlessly (~GLGame deletes board_). A connection that drops
  // while qualifying is silently reconnected ONCE (board_q_retried_) — a
  // phone's first WS connect after a radio wake routinely fails or crawls,
  // which field-tested as "no prompt on the first game, fine on the second".
  enum BoardPhase {
    BoardOff,         // nothing armed (the usual game over)
    BoardQualifying,  // qualify sent, waiting (nothing drawn yet)
    BoardPrompt,      // would place: YES/NO prompt on the card
    BoardUploading,   // submit in flight (progress line)
    BoardPlaced,      // done: "UPLOADED - RANK #N"
    BoardFailed,      // done: "UPLOAD FAILED" (+ short reason)
  };
  // Generous: the card sits until dismissed anyway, a late prompt is safe
  // (BOARD_PROMPT_ARM_MS anchors on when the PROMPT appears), and a cold
  // mobile connection — DNS + TLS + WS + a Durable Object spin-up on a
  // just-woken radio — can take well over the old 4 s.
  static const int BOARD_QUALIFY_TIMEOUT_MS = 15000;
  // The verification credential is minted asynchronously and re-minted per
  // read (steam/play_games/game_center identity backends), so the value
  // handed to a submit can be empty (mint not landed), stale (Game Center's
  // timestamp window), or — on the single-use platforms (Steam ticket, Play
  // Games code) — already consumed by an earlier send. The worker answers
  // all of these with reason "unverified". On that one reason the client
  // POLLS net_board_verify_credential_peek() (which does NOT re-mint, so it
  // can't flood the platform mint) until a GENUINELY fresh credential — one
  // different from the rejected value — appears, then consumes and resubmits
  // it ONCE. The first submit's own read already fired the next mint, so no
  // extra warm is needed; the poll just waits for it. Give up after this
  // long (generous, since a network-minted credential — Play Games, Game
  // Center — is a round-trip). One retry is also the per-connection submit
  // budget, so it can't loop.
  static const int BOARD_UPLOAD_RETRY_TIMEOUT_MS = 6000;
  // A freshly-shown prompt can't be answered for this long — the qualify
  // answer can arrive AFTER the card's 3 s game-over grace (its deadline
  // runs 15 s), so a keypress already in flight to leave must not land on
  // the just-appeared YES-default prompt.
  static const int BOARD_PROMPT_ARM_MS = 700;
  void board_maybe_start();      // at game-over finalize (after replay_finish)
  void board_tick();             // poll events + timeout (game_over only)
  // The card's nav while the prompt/result owns it. Logical keys (w/s move,
  // Enter confirms, Esc backs out = NO). True = input consumed; respects
  // the card's 3 s grace like every other game-over input.
  bool board_nav(char key);
  bool board_prompt_active() const {
    return board_phase_ == BoardPrompt || board_phase_ == BoardUploading;
  }
  NetBoard *board_ = nullptr;    // owned; non-null while a flow is live
  BoardPhase board_phase_ = BoardOff;
  int board_place_ = 0;          // projected (prompt) / final (placed) rank
  bool board_yes_ = true;        // prompt selection (YES default — plan)
  int board_deadline_ = 0;       // qualify timeout, current_time domain
  int board_prompt_shown_ = 0;   // current_time when BoardPrompt began
  bool board_up_retried_ = false; // an unverified upload has been retried once
  bool board_q_retried_ = false;  // the qualify connection was retried once
  // The qualify's board identity, kept for the reconnect's re-send (the
  // header was read once in board_maybe_start; score is board_score_).
  std::string board_q_season_;
  int board_q_players_ = 0;
  // Which best slot the finished run promoted (solo best.nrp or co-op
  // best_coop.nrp — best is per-board); what qualify reads and submit sends.
  std::string board_up_path_;
  int board_up_retry_deadline_ = 0; // current_time to give up polling for a
                                    // fresh credential (0 = not waiting)
  std::string board_up_sent_cred_;  // the rejected credential, so the retry
                                    // waits for a DIFFERENT one
  // Nav keys pressed (key-DOWN) while the prompt is up: keyboard_up acts
  // only on these, so a gameplay key held at death and released into the
  // prompt can't answer it. Cleared when the prompt opens.
  std::set<unsigned char> board_prompt_pressed_;
  std::string board_fail_reason_;
  uint32_t board_score_ = 0;     // the run's score, drawn on every card phase

  static const int step_size = 8;

  Point world;

  int generation;
  int last_tick, time_until_next_step, num_frames, current_time, time_between_steps;
  // Simulated ms ticked since the last draw, for the camera follow rate.
  int camera_delta_pending_ = 0;
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
  // The simulation has advanced since the last savegame write. Set once per
  // running tick, cleared by save_progress()'s write — so stacked save
  // triggers (pause -> focus loss -> quit, or menu-exit -> destructor)
  // produce ONE file write instead of identical back-to-back ones.
  bool save_dirty_ = false;
  // Lightweight frame telemetry for on-device perf hunts (Android logcat):
  // tick()/draw() bracket themselves; once a second, if the frame rate ran
  // below ~55 fps, one SDL_Log line breaks the frame down (sim vs GL
  // submission vs the rest = swap/present) with the live object counts.
  // Silent at healthy frame rates; no env plumbing needed on device.
  Uint32 perf_window_start_ = 0;
  Uint32 perf_last_frame_ = 0;  // gap detector: >500 ms = not-running window
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
  // ---- time-slow pickup (time_slow_pickup.h) ----
  // The world's wall-clock rate is divided by the factor for the pickup's
  // wall duration: each sim step still advances step_size ms of GAME time
  // (fire rate, thrust, physics all keep their in-game rates), the steps are
  // just SCHEDULED kTimeSlowFactor times further apart, exactly like the
  // debug slow-down key — but temporary, and never a cheat. The collector's
  // rotation is compensated (Ship::time_slow_rotation_comp) so their turn
  // rate feels unchanged in wall time: an aiming window, the pickup's whole
  // point. The countdown runs in SIM ms (wall / factor) so pause and the
  // intro freeze it and it rides the savegame deterministically.
  // ONLINE (PROTO 24) the host owns the effect like everything else and the
  // countdown + owner index ride every snapshot: net_apply_state adopts
  // them, the client's extrapolation loop multiplies its OWN step
  // scheduling by the same factor, and both machines slow in lockstep —
  // this is a shared *scheduling* multiplier on top of the pinned
  // time_between_steps, not the per-machine rate cheat the pin exists to
  // prevent (current_time and the stale gates are wall-based and
  // unaffected). Replays inherit the client path: the recorded wall-clock
  // record spacing IS the slow motion, and the mirrored factor keeps the
  // extrapolation between records at the matching rate.
  static const int kTimeSlowFactor = 2;
  static const int kTimeSlowWallMs = 10000;
  int time_slow_ms_left_ = 0;      // SIM ms remaining; 0 = inactive
  Ship *time_slow_ship_ = nullptr; // collector: owns the rotation comp
  bool time_slow_active() const { return time_slow_ms_left_ > 0; }
  int time_slow_wall_ms_remaining() const {
    return time_slow_ms_left_ * kTimeSlowFactor;
  }
  void start_time_slow(Ship *collector);
  // Per-sim-step countdown, shared by the host/offline loop and the
  // client/replay extrapolation loop: ticks the window down and drops the
  // rotation comp when it closes (client copies are re-asserted by every
  // snapshot apply; this keeps the in-between steps honest).
  void time_slow_step();
  // Start/end audio cues (a pitch dive when time slows, the reverse sweep
  // when it releases). Global state-change sounds like the level-countdown
  // tics — played unattenuated, deliberately not WorldSound (the effect has
  // no world position). The refractory timestamps dedupe the client's
  // countdown-vs-apply boundary races so an end can never double-play.
  void time_slow_cue(bool starting);
  int time_slow_start_cue_ms_ = -100000;  // current_time of the last cue
  int time_slow_end_cue_ms_ = -100000;
  // Re-level every player ship's self-played audio (thrusters included) for
  // the current listener distances. Called once per tick from both the
  // sim tick and the net-client tick.
  void update_player_sound_volumes();

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
  static const float time_slow_pickup_drop_chance;
  // Co-op revive: 10% per asteroid kill while a
  // partner is fully out, at most one in the world at a time.
  static const float revive_pickup_drop_chance;
  mutable WarpPass *warp_pass_;

  Mix_Chunk *tic_sound = NULL;
  Mix_Chunk *pickup_sound = NULL;
  Mix_Chunk *warp_sound = NULL;
  Mix_Chunk *station_explode_sound = NULL;
  Mix_Chunk *pause_music_sound = NULL;
  Mix_Chunk *time_slow_start_sound = NULL;
  Mix_Chunk *time_slow_end_sound = NULL;
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
  int all_weapons_ammo = 999;      // rounds per weapon; a numeric env value > 1 overrides
  // Screenshot harness (`hud off`): skip the HUD overlay and the minimap so
  // a composed shot is just the world. Only ShotScene ever sets it.
  bool shot_hide_hud_ = false;
  // Video harness: drop the playback chrome (REPLAY watermark, timeline,
  // control hints, the REPLAY ENDED card) so a captured replay looks like
  // gameplay rather than like someone watching a replay. The in-game HUD is
  // a separate decision (shot_hide_hud_) — a store video usually wants the
  // score and lives, and never wants the watermark. Only VideoCapture sets it.
  bool replay_hide_chrome_ = false;
};

#endif
