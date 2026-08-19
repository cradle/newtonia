#include "glgame.h"
#include "achievements.h"
#include "presence.h"
#include "invites.h"
#include "steam_build.h"
#include <cstdlib>
#include "asset_path.h"
#include "audio_volume.h"
#include "sound_cache.h"
#include "highscore.h"
#include "stats.h"
#include "preferences.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "glship.h"
#include "weapon/default.h"
#include "weapon/beam.h"
#include "weapon/lance.h"
#include "weapon/god_mode.h"
#include "net_resume.h"
#include "net_signal.h"
#include "net_transport.h"
#include "glcar.h"
#include "glstarfield.h"
#include "wrapped_point.h"
#include "intro.h"
#include "menu.h"
#include "menu_select.h"
#include "net_lobby.h"
#include "state.h"
#include "asteroid.h"
#include "asteroid_drawer.h"
#include "object.h"
#include "grid.h"
#include "view/overlay.h"
#include "typer.h"
#include "touch_controls.h"
#include "net_session.h"
#include "net_board.h"
#include "net_identity.h"
#include "replay.h"
#include "teleport.h"
#include "world_sound.h"
#include <math.h>
#include <cmath>
#include <SDL.h>

#include "gl_compat.h"
#include "mat4.h"
#include "mesh.h"

#include <iostream>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <algorithm>

// WorldSound listener trampoline: the hook is a plain function pointer so
// nothing outside GLGame (Asteroid, in particular) has to know the class
// exists. Installed by both base constructors, dropped by the destructor.
static float glgame_world_volume(const void *ctx, Point p) {
  return static_cast<const GLGame *>(ctx)->world_volume(p);
}

#ifdef __EMSCRIPTEN__
// Game over on the web: hand the run's result to the page (main.ts installs
// window.newtGameOver), which shows the "see where you'd rank" / "get it on
// Steam" banner over the canvas — the web build's stand-in for the Steam
// leaderboard prompt (NetBoard::create() is null on web). The score is the
// best local ship's, the number the game-over card headlines. Callers must
// skip playback (NetReplay): a replayed run's score is not the viewer's.
static void web_notify_game_over(std::list<GLShip *> *players) {
  int best = 0, count = 0;
  for (auto *gs : *players) {
    if (gs->ship->score > best) best = gs->ship->score;
    ++count;
  }
  EM_ASM({ if (window.newtGameOver) window.newtGameOver($0, $1); },
         best, count);
}
#endif

// First point along segment a->b entering the circle (centre target_pos,
// radius r), with the target translated to its wrapped copy nearest a —
// the same wrap idiom as the lance's asteroid march. Used by the lance
// ship/station hit passes (host resolution + the client's enemy pass);
// the entry point doubles as the impact position for debris/claims.
static bool lance_seg_circle_entry(const Point &a, const Point &b,
                                   const WrappedPoint &target_pos, float r,
                                   Point *entry) {
  Point p = target_pos.closest_to(a);
  float dx = b.x() - a.x(), dy = b.y() - a.y();
  float fx = a.x() - p.x(), fy = a.y() - p.y();
  float A = dx * dx + dy * dy;
  if (A <= 0.0f) return false;
  float B = 2.0f * (fx * dx + fy * dy);
  float C = fx * fx + fy * fy - r * r;
  float disc = B * B - 4.0f * A * C;
  if (disc < 0.0f) return false;
  float sq = sqrtf(disc);
  float t0 = (-B - sq) / (2.0f * A), t1 = (-B + sq) / (2.0f * A);
  if (t1 < 0.0f || t0 > 1.0f) return false;  // circle behind or beyond
  float t = t0 < 0.0f ? 0.0f : t0;           // started inside: entry = start
  if (entry) *entry = Point(a.x() + dx * t, a.y() + dy * t);
  return true;
}

// Rare, must-hear cues (station/mini-station destruction booms). In a
// heavy gen-20 firefight every dynamic mixing channel can be busy and
// Mix_PlayChannel(-1) silently drops the play; fall back to the reserved
// channels (0/1 — excluded from -1 allocation at platform init via
// Mix_ReserveChannels, so no boost/music loop ever lives there; the rest
// of the reserved span, 2..9, is WorldSound's pool — world_sound.h).
static void play_priority_chunk(Mix_Chunk *chunk, float vol) {
  if (chunk == NULL || vol <= 0.0f) return;
  Mix_VolumeChunk(chunk, (int)(MIX_MAX_VOLUME * vol));
  if (Mix_PlayChannel(-1, chunk, 0) != -1) return;
  Mix_PlayChannel(Mix_Playing(0) ? 1 : 0, chunk, 0);
}

// Arrow keys drive player 1 as built-in binding alternates (PlayerKeys
// defaults, preferences.h) — GLShip::input matches either slot of each
// KeyBinding, so no translation pass is needed here and an arrow bound to
// any player's action just works.
// Per-seat GLCar tints (FOURPLAYER.md D7): P2 keeps the classic orange;
// P3/P4 get their own hues so four cars read apart on screen, in the lives
// rows and on the minimap. Index 0 is unused (P1 is the blue GLShip).
static const float kSeatTints[MAX_PLAYERS][3] = {
    {0.0f, 0.0f, 0.0f},
    {255 / 255.0f, 69 / 255.0f, 0 / 255.0f},    // P2 orange (GLCar default)
    {80 / 255.0f, 220 / 255.0f, 100 / 255.0f},  // P3 green
    {200 / 255.0f, 120 / 255.0f, 255 / 255.0f}, // P4 violet
};
static const float *seat_tint(int player_index) {
  if (player_index < 1 || player_index >= MAX_PLAYERS) return NULL;
  return kSeatTints[player_index];
}

// Seat -> hull + tint (D7, amended): the two hull shapes alternate around
// the grid — P1 blue ship, P2 orange car, P3 green ship, P4 violet car —
// so diagonal partners never share both shape and colour.
static GLShip *make_seat_ship(const Grid &grid, int seat) {
  GLShip *gs;
  if (seat == 0) gs = new GLShip(grid, true);
  else if (seat == 2) gs = new GLShip(grid, true, seat_tint(seat));
  else gs = new GLCar(grid, true, seat_tint(seat));
  // PROTO 25 wire identity: seats are 1-based on the wire (0 = "not a
  // seated player"), while this helper's arg is the 0-based list index.
  gs->ship->net_seat = (uint8_t)(seat + 1);
  return gs;
}

// with_bindings=false applies only the seat's scalars (sensitivity, camera
// smoothing, rotate/fixed pref) — the pad-join path, where the pad is the
// controls but the seat's Options settings must still take effect.
static void set_player_keys(GLShip *gs, int player_index,
                            bool with_bindings = true) {
  if (player_index < 0) player_index = 0;
  if (player_index >= MAX_PLAYERS) player_index = MAX_PLAYERS - 1;
  PlayerKeys &k = g_prefs.player_keys[player_index];
  if (with_bindings) {
    gs->set_keys(k);
    gs->set_keymap_slot(player_index);
  }
  gs->set_keyboard_sensitivity(k.keyboard_sensitivity);
  gs->set_camera_smoothing(k.camera_smoothing);
  gs->set_rotate_view_pref(&k.rotate_view);
}

const int GLGame::default_world_width = 2500;
const int GLGame::default_world_height = 2500;
const int GLGame::default_num_asteroids = 3;
const int GLGame::extra_num_asteroids = 5;
const float GLGame::extra_life_drop_chance = 0.003125f;
const float GLGame::weapon_pickup_drop_chance = 0.0125f;
const float GLGame::mine_pickup_drop_chance = 0.0125f;
const float GLGame::giga_mine_pickup_drop_chance = 0.005f;
const float GLGame::missile_pickup_drop_chance = 0.0125f;
const float GLGame::shield_pickup_drop_chance = 0.0125f;
const float GLGame::god_mode_pickup_drop_chance = 0.0025f;
const float GLGame::beam_pickup_drop_chance = 0.00375f;
const float GLGame::lance_pickup_drop_chance = 0.0025f;
const float GLGame::shock_pickup_drop_chance = 0.003125f;
// Rare like god mode/lance: a whole-world effect, not an ammo top-up.
const float GLGame::time_slow_pickup_drop_chance = 0.0025f;
// Giga-mine tier: one drop arms 3 sentry drones.
const float GLGame::turret_pickup_drop_chance = 0.005f;
// Defined below; used by the host's MSG_SHOCK handler above it.
static bool shock_bolt_reaches(const std::vector<Point> &pts,
                               const WrappedPoint &pos, float radius);

// THE single Save::Pickup -> Pickup* factory, shared by the savefile-load
// constructor and the net client's snapshot rebuild (net_apply_state). Those
// used to be two hand-maintained copies of the same switch, and every new
// PickupType so far (Beam/Lance, then Shock) shipped with the net copy
// missed — the pickup existed in saves but was invisible on net clients.
// One switch, no default: a future enum value the switch doesn't handle is
// a -Wswitch warning here instead of an invisible-online runtime gap.
static Pickup *make_pickup(const Save::Pickup &sp) {
  WrappedPoint pos(sp.pos_x, sp.pos_y);
  switch (sp.type) {
    case Save::PickupType::Weapon:      return new WeaponPickup(pos, sp.weapon_index);
    case Save::PickupType::Mine:        return new MinePickup(pos);
    case Save::PickupType::GigaMine:    return new GigaMinePickup(pos);
    case Save::PickupType::Missile:     return new MissilePickup(pos);
    case Save::PickupType::Shield:      return new ShieldPickup(pos);
    case Save::PickupType::GodMode:     return new GodModePickup(pos);
    case Save::PickupType::ExtraLife:   return new ExtraLife(pos);
    case Save::PickupType::NovaCharge:  return new NovaChargePickup(pos);
    case Save::PickupType::Beam:        return new BeamPickup(pos);
    case Save::PickupType::Lance:       return new LancePickup(pos);
    case Save::PickupType::Revive:      return new RevivePickup(pos);
    case Save::PickupType::ShockWeapon: return new ShockPickup(pos);
    case Save::PickupType::TimeSlow:    return new TimeSlowPickup(pos);
    case Save::PickupType::Turret:      return new TurretPickup(pos);
  }
  return NULL;  // unknown value from a newer save: skip, matching old behavior
}
// Co-op revive: rolled INDEPENDENTLY of the cumulative table above, only
// while a partner is fully out of lives and no revive is already in the
// world — generous by design, it is the fallen player's only way back.
const float GLGame::revive_pickup_drop_chance = 0.1f;

GLGame::GLGame(SDL_GameController *controller, bool allow_dev_players) :
  State(),
  world(Point(default_world_width, default_world_height)),
  current_time(0),
  running(true),
  level_cleared(false),
  friendly_fire(g_prefs.friendly_fire),
  debug_grid(false),
  game_over(false),
  game_over_time(-1),
  grid(Grid(world, Point(Asteroid::max_radius*2,Asteroid::max_radius*2))) {
  time_between_steps = step_size;

  enemies = new std::list<GLShip*>;
  players = new std::list<GLShip*>;
  ship_objects = new std::list<Object*>;
  shock_targets = new std::list<Object*>;
  objects = new std::list<Asteroid*>;
  dead_objects = new std::list<Asteroid*>;
  pickups = new std::list<Pickup*>;
  black_holes = new std::list<BlackHole*>;
  hazards = new std::list<Hazard*>;

  net_clear_event_outboxes();

  WrappedPoint::set_boundaries(world);

  // World cues that belong to no ship (the shared Asteroid impact chunks)
  // ask us how far away they happened. Dropped again in the destructor.
  WorldSound::set_listener(glgame_world_volume, this);

  starfield = new GLStarfield(world, star_density_scale());
  warp_pass_ = new WarpPass();

  time_until_next_step = 0;
  num_frames = 0;

  generation = 0;
  // Dev/testing (beta builds only): NEWTONIA_START_GENERATION=N starts a new
  // game at generation N so the late game is reachable for playtesting
  // (achievement earnability past the black-hole wall, station fights).
  // Marked as a cheat below so it can never launder achievements (XR-057).
  bool dev_start = false;
  {
    const char *sg = SDL_getenv("NEWTONIA_START_GENERATION");
    if (sg != NULL && is_beta_feature_enabled() && atoi(sg) > 0) {
      dev_start = true;
      generation = atoi(sg);
      // Replicate the per-generation growth: +50 each rebuild, +3000 at 14.
      float grow = 50.0f * generation + (generation >= 14 ? 2950.0f : 0.0f);
      world += Point(grow, grow);
      grid = Grid(world, Point(Asteroid::max_radius*2, Asteroid::max_radius*2));
      WrappedPoint::set_boundaries(world);
      delete starfield;
      starfield = new GLStarfield(world, star_density_scale());
      std::cout << "DEV: starting at generation " << generation << std::endl;
    }
  }
  Asteroid::num_killable = 0;
  add_asteroids();
  grid.update((std::list<Object *>*)objects);

  GLShip *object = new GLShip(grid, true);
  object->ship->net_seat = 1;
  set_player_keys(object, 0);
  object->ship->is_local_player = true;
  // A new game begins legitimately: lift any XR-057 suppression left over
  // from a previous game's cheat keys.
  Achievements::new_game_started();
  // A new run gets a fresh id (rides the savegame + replay header so a
  // resume continues the same recording — REPLAY.md run-scoping).
  run_id_ = Replay::new_run_id();
  // NEWTONIA_ALL_WEAPONS: debug cheat granting the full arsenal each life.
  // Suppress achievements for the game like the other cheat paths (XR-057).
  // A numeric value > 1 sets the rounds per weapon (NEWTONIA_ALL_WEAPONS=30
  // makes drain tests quick); =1 or non-numeric keeps the 999 default.
  all_weapons_cheat = (SDL_getenv("NEWTONIA_ALL_WEAPONS") != NULL);
  if(all_weapons_cheat) {
    Achievements::note_cheat_used();
    int v = atoi(SDL_getenv("NEWTONIA_ALL_WEAPONS"));
    if(v > 1) all_weapons_ammo = v;
  }
  if(controller != NULL) {
    object->set_controller(controller);
  }
  object->ship->set_missile_asteroids((std::list<Object*>*)objects);
  ship_objects->push_back(object->ship);
  object->ship->set_missile_ships(ship_objects);
  object->ship->missiles_seek_players = friendly_fire;
  object->ship->set_shock_targets(shock_targets);
  object->ship->set_black_holes(black_holes);
  players->push_back(object);

  // Dev/testing (beta builds only): NEWTONIA_START_PLAYERS=N starts the game
  // with N local players, bypassing LOCAL_PLAYER_CAP so the 3-4P code paths
  // stay testable while the dark-launch gate holds (FOURPLAYER.md §3).
  // allow_dev_players is false on the delegating host ctor (net_mode_ isn't
  // set until after delegation, so the online guard can't catch it there —
  // an extra local seat would steal the peer's) and on the shots harness.
  // Cheat-marked like the other hooks (XR-057): a synthetic co-op run must
  // never chart or earn.
  {
    const char *sp = SDL_getenv("NEWTONIA_START_PLAYERS");
    if (allow_dev_players && sp != NULL && is_beta_feature_enabled() &&
        atoi(sp) > 1) {
      Achievements::note_cheat_used();
      int n = atoi(sp);
      if (n > MAX_PLAYERS) n = MAX_PLAYERS;
      for (int i = (int)players->size(); i < n; i++)
        add_local_player(NULL, /*with_keys=*/true, /*bypass_cap=*/true);
      std::cout << "DEV: starting with " << players->size() << " players"
                << std::endl;
    }
  }

  // Test hook (inert without the env var): ring one of each pickup around
  // the spawn so a driver can screenshot the full icon set (TESTING.md).
  if (SDL_getenv("NEWTONIA_TEST_SPAWN_PICKUPS")) {
    Point c(world.x() / 2.0f, world.y() / 2.0f);
    // Spawns are random; park the ship at the ring's centre so the whole
    // set is on the first screenshot (first-life respawn keeps the spot
    // unless an asteroid overlaps it).
    object->ship->position = WrappedPoint(c.x(), c.y());
    std::vector<Pickup*> ring = {
      new WeaponPickup(WrappedPoint(0, 0), 1), new MinePickup(WrappedPoint(0, 0)),
      new GigaMinePickup(WrappedPoint(0, 0)),  new MissilePickup(WrappedPoint(0, 0)),
      new ShieldPickup(WrappedPoint(0, 0)),    new GodModePickup(WrappedPoint(0, 0)),
      new NovaChargePickup(WrappedPoint(0, 0)), new BeamPickup(WrappedPoint(0, 0)),
      new LancePickup(WrappedPoint(0, 0)),     new ShockPickup(WrappedPoint(0, 0)),
      new RevivePickup(WrappedPoint(0, 0)),    new ExtraLife(WrappedPoint(0, 0)),
      new TimeSlowPickup(WrappedPoint(0, 0)),  new TurretPickup(WrappedPoint(0, 0)),
    };
    for (size_t i = 0; i < ring.size(); i++) {
      float a = i * 2.0f * (float)M_PI / ring.size();
      ring[i]->position = WrappedPoint(c.x() + cosf(a) * 200.0f,
                                       c.y() + sinf(a) * 200.0f);
      pickups->push_back(ring[i]);
    }
    // A line of 10 nova charges below the ring — enough to max the nova
    // (0-9) for co-op nova testing.
    for (int i = 0; i < 10; i++)
      pickups->push_back(new NovaChargePickup(
          WrappedPoint(c.x() - 450.0f + i * 100.0f, c.y() + 350.0f)));
  }

  station = NULL;//new GLStation(enemies, players);
  mini_station = NULL;

  if (dev_start) {
    // Spawn the hazards this generation would have accumulated, exactly as
    // the rebuild in tick() does, then suppress achievements for the game.
    if (generation >= 13)
      black_holes->push_back(new BlackHole(WrappedPoint(world.x() / 2.0f, world.y() / 2.0f)));
    if (generation >= 10)
      mini_station = new GLMiniStation(grid, players, (std::list<Object*>*)objects,
                                       hostile_aim_lead(generation));
    if (generation >= 14)
      station = new GLStation(grid, enemies, players, (std::list<Object*>*)objects,
                              hostile_aim_lead(generation));
    Achievements::note_cheat_used();
  }

  // Mid-game hazards accumulated by this generation (none at generation 0).
  add_hazards();

  update_presence();

  if(tic_sound == NULL) {
    tic_sound = Mix_LoadWAV(asset_path("audio/tic.wav").c_str());
    if(tic_sound == NULL) {
      std::cout << "Unable to load tic.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(pickup_sound == NULL) {
    pickup_sound = Mix_LoadWAV(asset_path("audio/pickup.wav").c_str());
    if(pickup_sound == NULL) {
      std::cout << "Unable to load pickup.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(warp_sound == NULL) {
    warp_sound = Mix_LoadWAV(asset_path("audio/warp.wav").c_str());
    if(warp_sound == NULL) {
      std::cout << "Unable to load warp.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(station_explode_sound == NULL) {
    station_explode_sound = Mix_LoadWAV(asset_path("audio/station_explode.wav").c_str());
    if(station_explode_sound == NULL) {
      std::cout << "Unable to load station_explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(pause_music_sound == NULL) {
    pause_music_sound = Mix_LoadWAV(asset_path("audio/pause.wav").c_str());
    if(pause_music_sound == NULL) {
      std::cout << "Unable to load pause.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(time_slow_start_sound == NULL) {
    time_slow_start_sound = Mix_LoadWAV(asset_path("audio/time_slow_start.wav").c_str());
    if(time_slow_start_sound == NULL) {
      std::cout << "Unable to load time_slow_start.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(time_slow_end_sound == NULL) {
    time_slow_end_sound = Mix_LoadWAV(asset_path("audio/time_slow_end.wav").c_str());
    if(time_slow_end_sound == NULL) {
      std::cout << "Unable to load time_slow_end.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
}

GLGame::GLGame(NetSession *session, SDL_GameController *controller)
  : GLGame(std::vector<NetSeated>(1, NetSeated{session, std::string(),
                                               NetIdentity()}),
           controller) {}

// B4b (PB-D6): the real host ctor — every seated waiting-room session
// becomes a NetPeer + a remote hull on its WELCOME seat. The 2P flow
// arrives here through the single-session delegator above with one entry
// and an empty jid/attestation (the lobby's post-construction
// net_set_peer_jid / net_apply_peer_attestation hand-over fills them),
// so at N=1 this is the old body with the peer adoption in a loop.
GLGame::GLGame(const std::vector<NetSeated> &seated,
               SDL_GameController *controller)
  : GLGame(controller, /*allow_dev_players=*/false) {
  net_mode_ = NetHost;
  Net::set_net_log_role(true);  // lobby set it too; belt & braces
  for (const NetSeated &s : seated) {
    NetPeer &p = net_peer_add();
    p.session = s.session;
    p.seat = (uint8_t)s.session->peer_seat();
    p.identity = s.session->peer_identity();
    p.jid = s.jid;
    p.attested = s.attested;
    // A jid-less entry in a MULTI-seat roster came through the LAN door
    // (waiting-room `lan#N` adoptions carry no worker jid) — the offline
    // display carve-out applies to that peer alone. The single-entry
    // delegator also passes an empty jid for worker sessions (the lobby
    // fills it post-construction), so 2P keeps the global-context rule.
    p.offline_paired = seated.size() > 1 && s.jid.empty();
    net_apply_attested(p.identity, s.attested);
  }
  if (!seated.empty()) net_peer_jid_ = seated.front().jid;
  // Greet the friend who just connected the moment the hosted game starts
  // ("GLENN JOINED"), at the attention-drawing banner spot above centre.
  // Composed again by the lobby's post-construction attestation/context
  // hand-over (net_refresh_join_banner) — at this point the identity is
  // claim-only and the strict default context hides the name.
  net_banner_ms_ = 3000;
  net_refresh_join_banner();
  Ship::net_report_bounces = true;  // PROTO 19: sim ricochets -> MSG_BOUNCE
  // A fresh game's player 1 starts dead (offline you wait out the initial
  // countdown or tap fire). Online the host just finished the lobby, so
  // start alive — and without this the client's bootstrap snapshot catches
  // the corpse mid-countdown: the restore resurrects the ghost, the first
  // extras kill it again, and the joiner watches player 1 "die" at start.
  players->front()->ship->respawn(grid, false);
  players->front()->ship->bullets.clear();  // no lethal spawn-flash debris
  for (NetPeer *p : net_peers_) {
    add_remote_player(p->seat);
    NET_LOG("net: ice path seat %d %s\n", (int)p->seat,
            p->session->transport()->connection_info().c_str());
  }
  // Co-op scoring parity: the initial hull costs a life, so each co-op
  // player fields the same total ship count as a solo run — otherwise a
  // pair banks two free hulls and the high scores aren't comparable.
  // Host-authoritative; the client mirrors lives via the snapshots.
  for (auto *gs : *players) gs->ship->lives -= 1;
  // The host's friendly-fire preference is the room rule; the client's HUD
  // shows its own preference until told otherwise.
  net_send_event(Net::EV_FRIENDLY_FIRE, friendly_fire ? 1u : 0u);
}

// Host process-death resume (NETPLAY.md): the OS killed the hosting
// process; the menu rebuilt the world from the online save slot and this
// constructor re-enters the state a live host is in after losing its
// peer. The first tick's rejoin poll parks the remote hull, pauses, and
// re-opens both rejoin doors (relay offer + LAN re-beacon); the reclaim
// countdown re-attaches to the room with the persisted token exactly like
// a mid-game signal drop (fresh TURN creds ride the reclaim reply), and
// the client's auto-rejoin retries meet it in the middle.
GLGame::GLGame(const Save::GameState &save, const std::string &room_code,
               const std::string &room_token, SDL_GameController *controller)
  : GLGame(save, controller) {
  net_mode_ = NetHost;
  Net::set_net_log_role(true);
  Ship::net_report_bounces = true;  // PROTO 19: sim ricochets -> MSG_BOUNCE
  net_room_code_ = room_code;
  net_room_token_ = room_token;
  // The save-restore base constructor made every saved ship a local
  // player; every seat but 1 is a REMOTE peer's — strip its bindings and
  // arm the host-side remote semantics (add_remote_player's arming minus
  // the spawn: restore_state already placed the hull). Seat-keyed since
  // PROTO 25 (an online v19 save names each ship's seat); a pre-v19
  // online save was always [P1, peer], and its positional seats (i+1)
  // pick the same ships.
  if (players->size() >= 2) {
    for (auto *gs : *players) {
      if (gs->ship->net_seat == 1) continue;
      gs->ship->is_local_player = false;
      gs->clear_keys();
      gs->set_controller(NULL);
      gs->ship->net_remote_gun = true;
    }
    players->front()->ship->net_report_shots = true;
  }
  net_peer_make().lost = true;
  net_signal_ = NetSignal::create();
  // Arm the shared reclaim countdown rather than connecting here: the
  // first tick runs net_host_signal_reclaim_tick, which clears the ICE
  // list and issues connect_host_reclaim — the exact path a dropped
  // socket takes, including its retry/backoff on failure.
  if (net_signal_) net_signal_retry_ms_ = 1;
  NET_LOG("net: resuming hosted room %s after process death\n",
          room_code.c_str());
  update_presence();
}

GLGame::~GLGame() {
  // Stop answering distance queries — no-op if a newer game already took
  // the hook over (states are built before their predecessor is deleted).
  WorldSound::clear_listener(this);
  // Deliberate teardown of a hosted room (quit to menu, game over, clean
  // app exit — the send_close below kills the room NOW): the process-death
  // resume ticket and online save go with it. A crash or OS kill never
  // runs this destructor, which is exactly what leaves them behind for
  // the menu's RESUME HOSTING row. Token cleared so save_progress below
  // can't re-mint the checkpoint it just deleted.
  if (net_mode_ == NetHost && !net_room_token_.empty()) {
    NetResume::clear_with_save();
    net_room_token_.clear();
  }
  save_progress();
  // Abandon-to-menu finalize: patch the header (clean, resumable) but keep
  // current.nrp in place — CONTINUE appends to it (REPLAY.md run-scoping).
  // After a game over the recorder is already finalized (no-op here).
  replay_finish(game_over);
  // Any in-flight leaderboard qualify/upload is abandoned with the state —
  // harmless by design (the worker's per-connection state dies with the
  // socket, and a half-sent submit is simply never finalized).
  delete board_;
  board_ = nullptr;
  delete replay_reader_;  // playback mode (R2); null otherwise
  // Host tore down while re-advertising the open slot (peer left, then we
  // returned to the menu / quit / game over): stop showing the "Join Game"
  // option.
  if (net_invite_advertised_) {
    Invites::clear_joinable();
    net_invite_advertised_ = false;
    NET_LOG("net: invite - room no longer joinable (game teardown)\n");
  }
  if (net_mode_ == NetClient || net_mode_ == NetReplay)
    Ship::net_quiet_respawn = false;
  if (net_mode_ == NetHost) Ship::net_report_bounces = false;
  // Leaving an online game: tell the peers (best effort — a hard close is
  // also detected via the channel-close path). B5: one lost seat must not
  // deny the healthy peers their goodbye — net_send_event skips
  // session-less peers itself.
  if (net_session() && !net_all_peers_lost())
    net_send_event(Net::EV_BYE);
  for (NetPeer *p : net_peers_) {
    delete p->session;  // closes + deletes the transport
    delete p;
  }
  net_peers_.clear();
  // Refusals still draining at the door (net_closing_) die with the game —
  // their peer is being told no either way.
  for (size_t ci = 0; ci < net_closing_.size(); ci++)
    delete net_closing_[ci].first;
  net_closing_.clear();
  delete net_assembler_;
  // Deliberate host teardown (quit to menu, game over): tell the relay to
  // kill the room NOW. A bare socket close would start the 2-minute
  // reclaim grace, leaving joiners — including this very player rejoining
  // their own code — waiting forever on "JOINING THE ROOM". A crash sends
  // nothing, so the grace window still covers real host drops.
  if (net_signal_) {
    net_signal_->send_close();
    net_signal_->close();
    delete net_signal_;
  }
  if (net_rehost_) {
    net_rehost_->close();
    delete net_rehost_;
  }
  if (net_lan_rehost_) {
    net_lan_rehost_->close();
    delete net_lan_rehost_;
  }
  // net_lan_announce_ stops itself in its destructor.
  // Cancel any verification ticket handles still outstanding (the lobby's
  // warm rides through the hand-off; host reclaims mint fresh ones). Skipped
  // when a client auto-rejoin handed the flow to a fresh NetLobby — that
  // lobby warmed its own ticket and releasing here would cancel it. No-op on
  // builds without a verify backend, and on a non-net game nothing was ever
  // warmed.
  if (net_active() && !net_handed_to_lobby_) net_release_verify_credentials();

  //TODO: Make erase, use boost::ptr_list? something better
  // std::erase(std::remove_if(v.begin(),v.end(),true), v.end());
  while(!players->empty()) {
    delete players->back();
    players->pop_back();
  }
  delete players;
  while(!enemies->empty()) {
    delete enemies->back();
    enemies->pop_back();
  }
  delete enemies;
  delete ship_objects;
  delete shock_targets;
  while(!objects->empty()) {
    delete objects->back();
    objects->pop_back();
  }
  delete objects;
  while(!dead_objects->empty()) {
    delete dead_objects->back();
    dead_objects->pop_back();
  }
  delete dead_objects;
  while(!pickups->empty()) {
    delete pickups->back();
    pickups->pop_back();
  }
  delete pickups;
  while(!black_holes->empty()) {
    delete black_holes->back();
    black_holes->pop_back();
  }
  delete black_holes;
  while(!hazards->empty()) {
    delete hazards->back();
    hazards->pop_back();
  }
  delete hazards;
  delete starfield;
  if(station != NULL)
    delete station;
  if(mini_station != NULL)
    delete mini_station;

  if(tic_sound != NULL) {
    Mix_FreeChunk(tic_sound);
  }
  if(pickup_sound != NULL) {
    Mix_FreeChunk(pickup_sound);
  }
  if(warp_sound != NULL) {
    Mix_FreeChunk(warp_sound);
  }
  if(station_explode_sound != NULL) {
    Mix_FreeChunk(station_explode_sound);
  }
  if(pause_music_channel >= 0) {
    Mix_HaltChannel(pause_music_channel);
  }
  if(pause_music_sound != NULL) {
    Mix_FreeChunk(pause_music_sound);
  }
  if(time_slow_start_sound != NULL) {
    Mix_FreeChunk(time_slow_start_sound);
  }
  if(time_slow_end_sound != NULL) {
    Mix_FreeChunk(time_slow_end_sound);
  }
  delete warp_pass_;
}

GLGame::GLGame(const Save::GameState &save, SDL_GameController *controller) :
  State(),
  world(Point(save.world_x, save.world_y)),
  generation(save.generation),
  current_time(save.current_time),
  running(true),
  level_cleared(save.level_cleared),
  friendly_fire(g_prefs.friendly_fire),
  debug_grid(false),
  game_over(false),
  game_over_time(-1),
  grid(Grid(Point(save.world_x, save.world_y),
            Point(Asteroid::max_radius*2, Asteroid::max_radius*2))) {
  time_between_steps = step_size;
  time_until_next_generation = save.time_until_next_generation;
  // Cheat suppression is game-scoped and rides the savegame, so quitting and
  // resuming doesn't launder it (XR-057).
  if (save.cheated) Achievements::note_cheat_used();
  else              Achievements::new_game_started();
  // Run-scoped replays (REPLAY.md): a save carrying a run_id may have a
  // matching current.nrp to continue (checked at replay_start); a pre-v17
  // save gets a fresh id, stamped back on the next save write.
  replay_resume_candidate_ = save.run_id != 0;
  run_id_ = save.run_id != 0 ? save.run_id : Replay::new_run_id();
  // NEWTONIA_ALL_WEAPONS: debug cheat granting the full arsenal each life.
  // Numeric value > 1 = rounds per weapon (see the new-game constructor).
  all_weapons_cheat = (SDL_getenv("NEWTONIA_ALL_WEAPONS") != NULL);
  if(all_weapons_cheat) {
    Achievements::note_cheat_used();
    int v = atoi(SDL_getenv("NEWTONIA_ALL_WEAPONS"));
    if(v > 1) all_weapons_ammo = v;
  }

  enemies = new std::list<GLShip*>;
  players = new std::list<GLShip*>;
  ship_objects = new std::list<Object*>;
  shock_targets = new std::list<Object*>;
  objects = new std::list<Asteroid*>;
  dead_objects = new std::list<Asteroid*>;
  pickups = new std::list<Pickup*>;
  black_holes = new std::list<BlackHole*>;
  hazards = new std::list<Hazard*>;

  net_clear_event_outboxes();

  WrappedPoint::set_boundaries(world);

  // See the new-game constructor: world cues that belong to no ship ask us
  // how far away they happened.
  WorldSound::set_listener(glgame_world_volume, this);

  starfield = new GLStarfield(world, star_density_scale());

  time_until_next_step = 0;
  num_frames = 0;

  // Restore asteroids
  Asteroid::num_killable = 0;
  for (const auto &sa : save.asteroids) {
    Asteroid *a = new Asteroid(sa.invincible, sa.invisible, sa.reflective,
                               sa.teleporting, sa.quantum, sa.tough, sa.armoured);
    a->restore_state(sa);
    objects->push_back(a);
  }
  grid.update((std::list<Object*>*)objects);

  // Restore pickups (make_pickup: the single shared PickupType switch)
  for (const auto &sp : save.pickups) {
    Pickup *p = make_pickup(sp);
    if (p) pickups->push_back(p);
  }

  // Restore black holes
  for (const auto &sbh : save.black_holes) {
    black_holes->push_back(new BlackHole(WrappedPoint(sbh.pos_x, sbh.pos_y)));
  }

  // Restore mid-game hazards (position, velocity and shockwave phase). A
  // seeker that had already been shot down was not saved, so nothing to make.
  for (const auto &sh : save.hazards) {
    hazards->push_back(Hazard::from_state(sh, world));
  }

  // Restore players — each seat gets its hull/tint (make_seat_ship). A
  // v19+ file names each entry's seat; pre-v19 (seat 0) falls back to the
  // old positional rule (entry i = seat i+1) — identical while seats are
  // dense, and the hull/tint/net_seat all follow the SAVED seat once B4
  // makes them sparse.
  for (const auto &sp : save.players) {
    bool is_p1 = players->empty();
    int seat_idx = sp.seat ? (int)sp.seat - 1 : (int)players->size();
    GLShip *gs = make_seat_ship(grid, seat_idx);
    set_player_keys(gs, (int)players->size());
    // Set before restore_state() so restored weapons attribute correctly.
    gs->ship->is_local_player = true;
    if (controller != NULL && is_p1) {
      gs->set_controller(controller);
    }
    gs->ship->set_missile_asteroids((std::list<Object*>*)objects);
    ship_objects->push_back(gs->ship);
    gs->ship->set_missile_ships(ship_objects);
    gs->ship->missiles_seek_players = friendly_fire;
    gs->ship->set_shock_targets(shock_targets);
    gs->ship->set_black_holes(black_holes);
    gs->ship->restore_state(sp, grid);
    gs->snap_camera_to_heading();
    players->push_back(gs);
  }

  // Resume an in-flight time-slow effect (v18): remaining sim ms plus the
  // collector's rotation compensation, restored onto the saved player index.
  // Clamped to the legal window like the net apply — this ctor also seeds
  // replay playback, and a downloaded (untrusted) replay's bootstrap
  // keyframe or a doctored save could otherwise start an over-long or
  // permanent slow (net_state_sane's bound is deliberately looser).
  if (save.time_slow_ms_left > 0 && !players->empty()) {
    time_slow_ms_left_ = save.time_slow_ms_left;
    if (time_slow_ms_left_ > kTimeSlowWallMs / kTimeSlowFactor)
      time_slow_ms_left_ = kTimeSlowWallMs / kTimeSlowFactor;
    uint8_t idx = 0;
    for (auto* gs : *players) {
      if (idx == save.time_slow_player) { time_slow_ship_ = gs->ship; break; }
      idx++;
    }
    if (time_slow_ship_ == NULL) time_slow_ship_ = players->front()->ship;
    time_slow_ship_->time_slow_rotation_comp = (float)kTimeSlowFactor;
  }

  // Assign any already-connected controllers to players that don't have one yet
  // (controller_added only fires for newly connected controllers, not pre-existing ones)
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (!SDL_IsGameController(i)) continue;
    SDL_GameController *ctrl = SDL_GameControllerOpen(i);
    if (!ctrl) continue;
    controller_added(ctrl);
  }

  if (save.station.present) {
    station = new GLStation(grid, enemies, players, (std::list<Object*>*)objects,
                            hostile_aim_lead(generation));
    station->restore_state(save.station, grid);
  } else {
    station = NULL;
  }

  // Restore the roaming mini-station (position + drift direction) exactly as it
  // was saved. If it had already been destroyed there is none to restore — the
  // next generation will spawn a fresh one as usual.
  if (save.mini_station.present && save.mini_station.alive) {
    mini_station = new GLMiniStation(grid, players, (std::list<Object*>*)objects,
                                     hostile_aim_lead(generation));
    mini_station->restore_state(save.mini_station);
  } else {
    mini_station = NULL;
  }
  warp_pass_ = new WarpPass();

  update_presence();

  if(tic_sound == NULL) {
    tic_sound = Mix_LoadWAV(asset_path("audio/tic.wav").c_str());
    if(tic_sound == NULL) {
      std::cout << "Unable to load tic.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(pickup_sound == NULL) {
    pickup_sound = Mix_LoadWAV(asset_path("audio/pickup.wav").c_str());
    if(pickup_sound == NULL) {
      std::cout << "Unable to load pickup.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(warp_sound == NULL) {
    warp_sound = Mix_LoadWAV(asset_path("audio/warp.wav").c_str());
    if(warp_sound == NULL) {
      std::cout << "Unable to load warp.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(station_explode_sound == NULL) {
    station_explode_sound = Mix_LoadWAV(asset_path("audio/station_explode.wav").c_str());
    if(station_explode_sound == NULL) {
      std::cout << "Unable to load station_explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(pause_music_sound == NULL) {
    pause_music_sound = Mix_LoadWAV(asset_path("audio/pause.wav").c_str());
    if(pause_music_sound == NULL) {
      std::cout << "Unable to load pause.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(time_slow_start_sound == NULL) {
    time_slow_start_sound = Mix_LoadWAV(asset_path("audio/time_slow_start.wav").c_str());
    if(time_slow_start_sound == NULL) {
      std::cout << "Unable to load time_slow_start.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(time_slow_end_sound == NULL) {
    time_slow_end_sound = Mix_LoadWAV(asset_path("audio/time_slow_end.wav").c_str());
    if(time_slow_end_sound == NULL) {
      std::cout << "Unable to load time_slow_end.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
}

void GLGame::save_progress() {
  Stats::flush();  // lifetime stats persist regardless of campaign-save eligibility
  // Online play never touches the local solo save (see NETPLAY.md): the
  // hosted world is not the solo game, and the game-over delete below
  // would otherwise wipe real progress. The host instead checkpoints the
  // process-death resume slot (ticket + online save) at these moments.
  if (net_mode_ != NetOff) {
    net_host_resume_persist();
    return;
  }
  if (game_over) return;
  if (score_saved) return;
  for (auto* gs : *players) {
    if (gs->ship->is_alive() || gs->ship->lives > 0) {
      // Dedupe stacked triggers: skip the write when the sim hasn't advanced
      // since the last one (paused pause->quit, menu-exit->destructor). The
      // game-over delete below is not gated — a roster can only become
      // all-dead through a running tick, which sets the flag anyway.
      if (save_dirty_) {
        Save::save_game(build_save_data());
        save_dirty_ = false;
      }
      return;
    }
  }
  // All players dead with no lives remaining — game over, delete any save
  if (!save_deleted_) {
    Save::delete_save();
    save_deleted_ = true;
  }
}

// Host process-death resume (NETPLAY.md): checkpoint the hosted session —
// the reclaim ticket beside the dedicated online world save — so a killed
// process can offer RESUME HOSTING on relaunch. No-op unless this machine
// hosts a token-bearing room (clients, LAN/manual sessions and solo games
// have nothing to reclaim). A game over ends the session's resumability
// instead: both files are deleted, mirroring the solo save's delete.
void GLGame::net_host_resume_persist() {
  if (net_mode_ != NetHost || net_room_token_.empty()) return;
  if (game_over) {
    NetResume::clear_with_save();
    return;
  }
  Save::online_save_game(build_save_data());
  NetResume::write(net_room_code_, net_room_token_);
}

Save::GameState GLGame::build_save_data(bool include_asteroids) const {
  Save::GameState s;
  s.generation                 = generation;
  s.world_x                    = world.x();
  s.world_y                    = world.y();
  s.level_cleared              = level_cleared;
  s.time_until_next_generation = time_until_next_generation;
  s.current_time               = current_time;
  s.cheated                    = Achievements::unlocks_suppressed();
  s.run_id                     = run_id_;
  // v18: the in-flight time-slow effect rides the save (like god mode's
  // remaining ms riding its weapon entry) — and, since snapshots serialize
  // through this same struct, it is ALSO how the effect replicates online
  // and into replays: net_apply_state adopts these two scalars from every
  // keyframe/delta (PROTO 24).
  s.time_slow_ms_left          = time_slow_ms_left_;
  if (time_slow_ship_ != NULL) {
    uint8_t idx = 0;
    for (auto* gs : *players) {
      if (gs->ship == time_slow_ship_) { s.time_slow_player = idx; break; }
      idx++;
    }
  }

  for (auto* gs : *players)
    s.players.push_back(gs->ship->capture_state());

  // The delta path sends asteroids itself (fresh/dirty/removed diff) and
  // discards this list, so capturing every asteroid here — ~200 bytes each
  // via capture_state, 9x/s — is pure waste on that path.
  if (include_asteroids)
    for (auto* a : *objects)
      s.asteroids.push_back(a->capture_state());

  for (auto* p : *pickups) {
    Save::Pickup sp;
    sp.pos_x = p->position.x();
    sp.pos_y = p->position.y();
    sp.weapon_index = -1;
    if (WeaponPickup *wp = dynamic_cast<WeaponPickup*>(p)) {
      sp.type = Save::PickupType::Weapon;
      sp.weapon_index = wp->get_weapon_index();
    } else if (dynamic_cast<MinePickup*>(p)) {
      sp.type = Save::PickupType::Mine;
    } else if (dynamic_cast<GigaMinePickup*>(p)) {
      sp.type = Save::PickupType::GigaMine;
    } else if (dynamic_cast<MissilePickup*>(p)) {
      sp.type = Save::PickupType::Missile;
    } else if (dynamic_cast<ShieldPickup*>(p)) {
      sp.type = Save::PickupType::Shield;
    } else if (dynamic_cast<GodModePickup*>(p)) {
      sp.type = Save::PickupType::GodMode;
    } else if (dynamic_cast<ExtraLife*>(p)) {
      sp.type = Save::PickupType::ExtraLife;
    } else if (dynamic_cast<NovaChargePickup*>(p)) {
      sp.type = Save::PickupType::NovaCharge;
    } else if (dynamic_cast<BeamPickup*>(p)) {
      sp.type = Save::PickupType::Beam;
    } else if (dynamic_cast<LancePickup*>(p)) {
      sp.type = Save::PickupType::Lance;
    } else if (dynamic_cast<RevivePickup*>(p)) {
      sp.type = Save::PickupType::Revive;
    } else if (dynamic_cast<ShockPickup*>(p)) {
      sp.type = Save::PickupType::ShockWeapon;
    } else if (dynamic_cast<TimeSlowPickup*>(p)) {
      sp.type = Save::PickupType::TimeSlow;
    } else if (dynamic_cast<TurretPickup*>(p)) {
      sp.type = Save::PickupType::Turret;
    } else {
      continue; // unknown pickup type, skip
    }
    s.pickups.push_back(sp);
  }

  for (auto* bh : *black_holes)
    s.black_holes.push_back({bh->position.x(), bh->position.y()});

  // A destroyed seeker (still around only for its fading debris) isn't worth
  // persisting; everything else is captured with its live state.
  for (auto* h : *hazards)
    if (h->is_alive())
      s.hazards.push_back(h->capture_state());

  if (station) {
    s.station = station->capture_state();
  } else {
    s.station.present = false;
  }

  if (mini_station) {
    s.mini_station = mini_station->capture_state();
  } else {
    s.mini_station.present = false;
  }

  return s;
}

void GLGame::add_asteroids() {
  while(Asteroid::num_killable < (default_num_asteroids + generation * extra_num_asteroids)) {
    objects->push_back(new Asteroid(false));
    if(generation > 0) objects->push_front(new Asteroid(true));
  }
  int num_invisible = (generation >= 4) ? (generation - 4) / 5 + 1 : 0;
  for(int i = 0; i < num_invisible; i++) {
    objects->push_back(new Asteroid(false, true));
  }
  int num_reflective = (generation >= 2) ? (generation - 2) / 2 + 1 : 0;
  for(int i = 0; i < num_reflective; i++) {
    objects->push_front(new Asteroid(false, false, true));
  }
  int num_teleporting = (generation >= 3) ? (generation - 3) / 2 + 1 : 0;
  for(int i = 0; i < num_teleporting; i++) {
    objects->push_back(new Asteroid(false, false, false, true));
  }
  int num_quantum = (generation >= 5) ? (generation - 5) / 3 + 1 : 0;
  for(int i = 0; i < num_quantum; i++) {
    objects->push_back(new Asteroid(false, false, false, false, true));
  }
  int num_tough = (generation >= 6) ? (generation - 6) / 2 + 1 : 0;
  for(int i = 0; i < num_tough; i++) {
    objects->push_back(new Asteroid(false, false, false, false, false, true));
  }
  int num_armoured = (generation >= 7) ? (generation - 7) / 2 + 1 : 0;
  for(int i = 0; i < num_armoured; i++) {
    objects->push_back(new Asteroid(false, false, false, false, false, false, true));
  }
  int num_phasing = (generation >= 8) ? (generation - 8) / 2 + 1 : 0;
  for(int i = 0; i < num_phasing; i++) {
    objects->push_back(new Asteroid(false, false, false, false, false, false, false, true));
  }
}

void GLGame::add_hazards() {
  // Counts scale with generation, mirroring the special-asteroid formulas: the
  // introducing level gets one, later levels accumulate more.
  //   PULSAR from generation 9  (displayed level 10)
  //   COMET  from generation 11 (displayed level 12)
  //   SEEKER from generation 12 (displayed level 13)
  int num_pulsar = (generation >= 9)  ? (generation - 9)  / 3 + 1 : 0;
  int num_comet  = (generation >= 11) ? (generation - 11) / 2 + 1 : 0;
  int num_seeker = (generation >= 12) ? (generation - 12) / 2 + 1 : 0;
  for(int i = 0; i < num_pulsar; i++) hazards->push_back(new Hazard(Hazard::PULSAR, world));
  for(int i = 0; i < num_comet;  i++) hazards->push_back(new Hazard(Hazard::COMET,  world));
  for(int i = 0; i < num_seeker; i++) hazards->push_back(new Hazard(Hazard::SEEKER, world));
}

Hazard *GLGame::first_hazard(Hazard::Kind kind) const {
  for(auto* h : *hazards)
    if(h->kind_of() == kind) return h;
  return NULL;
}

void GLGame::play_hazard_hit_sound(Mix_Chunk *snd) {
  if(snd == NULL) return;
  static Uint32 last = UINT32_MAX;
  Uint32 now = SDL_GetTicks();
  if(now - last >= 50) {  // same guard idiom as the station bullet thud
    last = now;
    Mix_PlayChannel(-1, snd, 0);
  }
}

void GLGame::shed_comet_fragment(const Hazard *comet) {
  // A normal killable asteroid, shrunk to a chunk and flung outward from the
  // comet's heading. num_killable is bumped by the constructor, so the piece
  // counts toward clearing the level like any other asteroid.
  Asteroid *frag = new Asteroid(false);
  frag->comet_fragment = true;          // drawn solid white, like the comet
  frag->radius = 16.0f + rand() % 14;   // 16–29: a small shard
  frag->radius_squared = frag->radius * frag->radius;
  frag->value = std::min(100, std::max(1, (int)(1600.0f / frag->radius)));
  frag->position = WrappedPoint(comet->position.x(), comet->position.y());
  float ang = rand() / (float)RAND_MAX * 2.0f * (float)M_PI;
  float sp  = 0.15f + rand() / (float)RAND_MAX * 0.15f;
  frag->velocity = comet->velocity * 0.5f + Point(cosf(ang) * sp, sinf(ang) * sp);
  objects->push_back(frag);
}

void GLGame::maybe_start_intro() {
  // No Intro states online: both machines must keep ticking in lockstep
  // with the snapshot stream (a 2 s banner replaces them — Phase 8).
  if (net_mode_ != NetOff) return;
  // Never after game over: the background world keeps simulating behind
  // the GAME OVER card, and if it clears its level the generation still
  // advances — but handing the state to an Intro would steal the screen
  // AND the input from the card (the leaderboard prompt's YES landed on
  // "PRESS FIRE TO START"; caught by the e2e's time-cheated S5).
  if (game_over) return;
  const char *name = NULL;
  Asteroid *display = NULL;
  Intro::Kind kind = Intro::ASTEROID;
  int hazard_kind = -1;  // Hazard::Kind for Intro::HAZARD, else unused
  switch (generation) {
    case 1:  display = new Asteroid(true);
             name = "INVINCIBLE";  break;
    case 2:  display = new Asteroid(false, false, true);
             name = "REFLECTIVE";  break;
    case 3:  display = new Asteroid(false, false, false, true);
             name = "TELEPORTING"; break;
    case 4:  display = new Asteroid(false, true);
             name = "INVISIBLE";   break;
    case 5:  display = new Asteroid(false, false, false, false, true);
             name = "QUANTUM";     break;
    case 6:  display = new Asteroid(false, false, false, false, false, true);
             name = "TOUGH";       break;
    case 7:  display = new Asteroid(false, false, false, false, false, false, true);
             name = "ARMOURED";    break;
    case 8:  display = new Asteroid(false, false, false, false, false, false, false, true);
             name = "PHASING";     break;
    case 9:  if (first_hazard(Hazard::PULSAR)) { kind = Intro::HAZARD; name = "PULSAR";
             hazard_kind = Hazard::PULSAR; } break;
    case 10: if (mini_station != NULL)  { kind = Intro::MINI_STATION; name = "MINI STATION"; }  break;
    case 11: if (first_hazard(Hazard::COMET))  { kind = Intro::HAZARD; name = "COMET";
             hazard_kind = Hazard::COMET; }  break;
    case 12: if (first_hazard(Hazard::SEEKER)) { kind = Intro::HAZARD; name = "SEEKER";
             hazard_kind = Hazard::SEEKER; } break;
    case 13: if (!black_holes->empty()) { kind = Intro::BLACK_HOLE;   name = "BLACK HOLE"; }    break;
    case 14: if (station != NULL)       { kind = Intro::STATION;      name = "ENEMY STATION"; } break;
    default: return;
  }
  if (name == NULL) return;

  // The intro adopts this state (ownership transfers, so the StateManager
  // won't delete it) and hands it back when the player presses fire.
  request_state_change(new Intro(this, kind, name, display, hazard_kind), true);
}

void GLGame::release_player_controls() {
  for (auto *glship : *players)
    glship->release_controls();
}

bool GLGame::all_players_out() const {
  if (game_over) return true;
  if (players->empty()) return false;
  for (auto *gs : *players)
    if (gs->ship->is_alive() || gs->ship->lives > 0) return false;
  return true;
}

void GLGame::toggle_pause(bool broadcast) {
  // A finished game can't be paused: the GAME OVER card owns the screen and
  // the only input that matters is fire-for-menu — pausing would stack the
  // "Paused" overlay on top of the card (seen on a net client, but the pause
  // key reached here in every mode). Unpausing stays allowed for a game that
  // ends while already paused (e.g. the host leaving a paused game is
  // terminal for a spectating client).
  if (running && all_players_out()) return;
  running = !running;
  // The pause menu always opens on RESUME: leaving the highlight where the
  // last pause left it would put EXIT TO MENU under a reflexive confirm.
  // The roster closes with the pause screen for the same reason — every
  // pause opens on the same screen, however the last one ended.
  if (!running) pause_selection_ = PAUSE_RESUME;
  roster_active_ = false;
  // Pausing auto-saves (the long-documented behavior — it previously only
  // happened via the focus-loss path, so quitting in a way that skips the
  // exit hooks (force-kill, crash, a killed mobile-web tab) lost everything
  // since the last death or level clear). Covers the pause key, controller
  // Start, and the touch pause zones; save_progress() itself refuses online
  // games, game over, and dead rosters.
  if (!running) save_progress();
  // Replay checkpoint (REPLAY.md): pause is a lifecycle flush point — the
  // record chunk since the last level boundary hits the disk here.
  if (!running && replay_) replay_->flush();
  // Online, pausing is shared state: tell the peers (unless this call IS
  // a peer's event being applied). B5: keep telling the LIVE peers while
  // one seat is out — the shared-pause contract must not break for them.
  if (broadcast && net_mode_ != NetOff && net_session() &&
      !net_all_peers_lost())
    net_send_event(running ? Net::EV_RESUME : Net::EV_PAUSE);
  // Pausing stops keyboard()/controller() forwarding to the ships, so any
  // release that happens while paused is lost — drop everything now, or a
  // thrust held over the pause stays latched ON after unpausing.
  if (!running) release_player_controls();
  if (running) {
    if (pause_music_channel >= 0) {
      Mix_HaltChannel(pause_music_channel);
      pause_music_channel = -1;
    }
    Mix_Resume(-1);
  } else {
    // The pause tune starts after the pause so its own channel keeps playing.
    Mix_Pause(-1);
    if (pause_music_sound != NULL) {
      // Follows the MUSIC volume (audio_volume.h) — chunk-level, like the
      // intro tune: this chunk is this game's own, and a lowered dynamic
      // CHANNEL would leak its volume to the channel's next user.
      Mix_VolumeChunk(pause_music_sound,
                      (int)(MIX_MAX_VOLUME * AudioVolume::music_scale() + 0.5f));
      pause_music_channel = Mix_PlayChannel(-1, pause_music_sound, -1);
    }
  }
}

// A replay ends in one of two ways, and BOTH draw the shared RETURN TO
// MENU row: the recorded run died (GAME OVER, after its 3 s grace so a
// stray key can't skip past the ending), or the records simply ran out
// (REPLAY ENDED — an abandoned run, or one whose tail never made it to
// disk). Only the first used to answer a confirm, so Enter on a REPLAY
// ENDED card did nothing at all and Esc was the sole way out — a row that
// says EXIT TO MENU and ignores Enter (field, 2026-07-29). Nothing needs
// protecting once the records are exhausted: the world is frozen and
// there is nothing left to watch.
bool GLGame::replay_exit_offered() const {
  return replay_finished_ || (game_over && !game_over_grace_active());
}

bool GLGame::pause_menu_active() const {
  if (running) return false;
  // The seat roster is drawn over the pause menu and owns input while it is
  // up — one screen answering at a time, the same rule the disconnect card
  // and help card follow.
  if (roster_active_) return false;
  // Touch draws no selection cursor on any screen, and the pause screen
  // there already has both actions as touch targets.
  if (is_touch_mode()) return false;
  // Nothing left to resume — the GAME OVER card owns the screen.
  if (all_players_out()) return false;
  // The connection-lost card owns input (keyboard_up/controller return
  // before the pause ladder, answering only the card's own EXIT TO MENU
  // row), so the menu must not be drawn under it. Without this, the host
  // pausing and then leaving showed the client a highlighted RESUME over
  // a second EXIT TO MENU, and neither answered (field, 2026-08-07).
  if (net_card_owns_input()) return false;
  // A paused REPLAY gets the menu too. It used to be excluded — "a replay's
  // pause is a playback control, not a menu" — but the overlay draws the
  // two rows from `!running` alone, so a paused replay showed RESUME
  // highlighted and EXIT TO MENU under it and answered neither: only ESC
  // did anything (field, 2026-07-29). Drawn and interactive have to agree,
  // and both rows mean something here — the recording resumes, or you leave
  // it. `pause_nav`'s exit is safe: `save_progress` no-ops for any net mode
  // but NetOff, so nothing writes the ghost world over a real save.
  // The help card takes the pause text's place, so the menu isn't drawn.
  // Navigating a menu you cannot see is how a game gets quit by accident.
  for (auto *gs : *players)
    if (gs->showing_help()) return false;
  return true;
}

void GLGame::pause_nav(unsigned char key) {
  if (MenuSelect::move(key, pause_selection_, pause_row_count())) return;
  if (!MenuSelect::is_confirm(key)) return;
  switch (pause_row_at(pause_selection_)) {
    case PAUSE_RESUME:
      toggle_pause();
      break;
    case PAUSE_PLAYERS:
      roster_active_ = true;
      roster_selection_ = 0;
      break;
    default:
      // Exactly what the menu key does — save first, then hand over.
      save_progress();
      request_state_change(new Menu());
      break;
  }
}

// ---- Seat input roster ------------------------------------------------

// The seat at list position i (players is a list, so no operator[]).
static GLShip *seat_ship_at(std::list<GLShip*> *players, int i) {
  if (i < 0 || i >= (int)players->size()) return NULL;
  auto it = players->begin();
  std::advance(it, i);
  return *it;
}

// A keyboard cluster's label, read from the slot's own bindings so a
// hand-edited p3_*/p4_* INI shows the truth rather than a hardcoded guess:
// the four direction primaries, e.g. "WASD" for slot 0 and "IJKL" for 1.
static std::string cluster_label(int slot) {
  if (slot < 0 || slot >= MAX_PLAYERS) return std::string();
  const PlayerKeys &k = g_prefs.player_keys[slot];
  auto glyph = [](int key) -> char {
    if (key >= 33 && key <= 126) return (char)toupper(key);
    return '?';
  };
  if (k.thrust.primary() == 0) return std::string();  // slot binds nothing
  char buf[5] = { glyph(k.thrust.primary()), glyph(k.left.primary()),
                  glyph(k.reverse.primary()), glyph(k.right.primary()), 0 };
  return std::string(buf);
}

// 1-based position among the connected game controllers — stable enough to
// tell two pads apart on screen, and far shorter than SDL's product names.
static int pad_number(SDL_JoystickID id) {
  int n = SDL_NumJoysticks(), num = 0;
  for (int i = 0; i < n; i++) {
    if (!SDL_IsGameController(i)) continue;
    num++;
    if (SDL_JoystickGetDeviceInstanceID(i) == id) return num;
  }
  return 0;
}

bool GLGame::roster_available() const {
  // Two contexts, one screen (FOURPLAYER.md O3): offline it re-binds local
  // inputs, and on the HOST online it lists the peers with a KICK action.
  // Not on a client (other people's seats aren't theirs to manage) and not
  // on touch (no cursor; one local player by construction).
  if (is_touch_mode()) return false;
  return net_mode_ == NetOff || net_mode_ == NetHost;
}

// True when this row is a remote pilot the host may remove, rather than a
// local seat whose input can be re-bound.
bool GLGame::roster_row_is_peer(int row) const {
  if (net_mode_ != NetHost) return false;
  GLShip *gs = seat_ship_at(players, row);
  if (!gs) return false;
  for (const NetPeer *p : net_peers_)
    if (p->seat == gs->ship->net_seat)
      // A lost or parked seat has nobody to remove — the pilot is already
      // gone (kicked, or dropped and rejoining). Offering KICK there is a
      // button that does nothing, which is how a screen teaches people to
      // distrust it.
      return !p->lost && !p->parked;
  return false;
}

// Can this row's pilot be BANNED, or only kicked? Only a worker-attested
// name makes a ban durable — see net_identity_bannable. Everything else
// (a legacy peer, a badge-only backend, an unverified claim) gets KICK
// alone rather than a button that quietly does less than it says.
bool GLGame::roster_row_can_ban(int row) const {
  const NetPeer *p = const_cast<GLGame *>(this)->roster_peer_at(row);
  return p && !net_identity_anonymous(p->identity);
}

// The peer occupying a roster row, or null.
GLGame::NetPeer *GLGame::roster_peer_at(int row) {
  GLShip *gs = seat_ship_at(players, row);
  if (!gs) return NULL;
  for (NetPeer *p : net_peers_)
    if (p->seat == gs->ship->net_seat) return p;
  return NULL;
}

int GLGame::roster_row_count() const {
  int seats = (int)players->size();
  // Online the ADD row would be a lie — seats fill from the room, not from
  // this machine — so the host's list is the seats plus the one thing that
  // IS the host's to decide: who is allowed to take the empty ones. Every
  // layout ends with the exit row (see roster_row_is_exit).
  if (net_mode_ != NetOff)
    return seats + (roster_has_anon_row() ? 1 : 0) + 1;
  return (seats < MAX_PLAYERS ? seats + 1 : seats) + 1;
}

// The trailing BACK row. The screen used to end in an "ESC BACK" hint: a
// key spelled out under a list whose every other row is picked with the
// cursor, and the only way out the cursor could not reach. It is a row now,
// doing what Esc does — step back to the pause menu (which is where the
// game's own EXIT TO MENU lives, one row away). Esc still works.
bool GLGame::roster_row_is_exit(int row) const {
  return row == roster_row_count() - 1;
}

// The ALLOW ANONYMOUS PLAYERS row: the host's admission policy, so it shows
// for the host only. Its lobby twin is the waiting room's row of the same
// name — one preference (Preferences::allow_anonymous), settable wherever
// the host is standing when they think of it.
bool GLGame::roster_has_anon_row() const { return net_mode_ == NetHost; }

bool GLGame::roster_row_is_anon(int row) const {
  return roster_has_anon_row() && row == (int)players->size();
}

GLGame::SeatInput GLGame::roster_seat_input(int seat) const {
  SeatInput in;
  GLShip *gs = seat_ship_at(players, seat);
  if (!gs) return in;
  if (gs->has_controller()) {
    in.kind = SeatInput::Pad;
    in.pad = gs->controller_id();
  } else if (gs->has_keys()) {
    in.kind = SeatInput::Keys;
    in.slot = gs->keymap_slot();
  }
  return in;
}

std::string GLGame::roster_seat_label(int seat) const {
  GLShip *gs = seat_ship_at(players, seat);
  if (!gs) return "EMPTY";
  std::string out;
  if (gs->has_keys()) out = cluster_label(gs->keymap_slot());
  if (gs->has_controller()) {
    char buf[16];
    snprintf(buf, sizeof buf, "PAD %d", pad_number(gs->controller_id()));
    // A seat can legitimately hold both (a pad-started solo game keeps the
    // keyboard too), so say so rather than hiding one of them.
    out = out.empty() ? std::string(buf) : out + " + " + buf;
  }
  return out.empty() ? "NONE" : out;
}

// Every input this machine can offer a seat: nothing, each keyboard cluster
// that actually binds keys, then each connected pad.
std::vector<GLGame::SeatInput> GLGame::roster_input_options() const {
  std::vector<SeatInput> out;
  out.push_back(SeatInput());  // None
  for (int s = 0; s < MAX_PLAYERS; s++) {
    if (cluster_label(s).empty()) continue;
    SeatInput in;
    in.kind = SeatInput::Keys;
    in.slot = s;
    out.push_back(in);
  }
  int n = SDL_NumJoysticks();
  for (int i = 0; i < n; i++) {
    if (!SDL_IsGameController(i)) continue;
    SeatInput in;
    in.kind = SeatInput::Pad;
    in.pad = SDL_JoystickGetDeviceInstanceID(i);
    out.push_back(in);
  }
  return out;
}

// Move `in` onto `seat`, taking it off whatever seat held it. An input drives
// exactly one ship: two seats sharing a cluster would answer the same keys,
// which is the confusion this screen exists to end.
void GLGame::roster_apply(int row, const SeatInput &in) {
  if (row >= (int)players->size()) {
    // The ADD row: an input lands a new seat. NONE would seat a ship nobody
    // can fly, so it stays a no-op.
    if (in.kind == SeatInput::None) return;
    if ((int)players->size() >= MAX_PLAYERS) return;
    add_local_player(NULL, /*with_keys=*/false);
    if (row >= (int)players->size()) return;  // refused (e.g. p1 out)
  }
  GLShip *target = seat_ship_at(players, row);
  if (!target) return;
  for (auto *gs : *players) {
    if (gs == target) continue;
    if (in.kind == SeatInput::Keys && gs->keymap_slot() == in.slot)
      gs->clear_keys();
    if (in.kind == SeatInput::Pad && gs->is_my_controller_id(in.pad))
      gs->set_controller(NULL);
  }
  // Exclusive per seat: picking one input clears the other, so the row says
  // what actually drives the ship.
  target->release_controls();  // a held key/stick must not latch across this
  switch (in.kind) {
    case SeatInput::Keys:
      target->set_controller(NULL);
      set_player_keys(target, in.slot);
      break;
    case SeatInput::Pad:
      target->clear_keys();
      target->set_controller(SDL_GameControllerFromInstanceID(in.pad));
      break;
    default:
      target->clear_keys();
      target->set_controller(NULL);
      break;
  }
}

void GLGame::roster_nav(unsigned char key) {
  // Host rows: left/right picks WHICH removal (kick, which they can come
  // back from, or ban, which they can't) — the same left/right that cycles
  // a local seat's input, on rows where rebinding is meaningless. Confirm
  // then ARMS it and a second confirm on the same row does it: ending
  // someone's game is not something a stray Enter — the key that opened
  // this screen — should be able to do.
  // The policy row: left/right and confirm all mean the same thing on a
  // two-state row, so answer all three rather than making the host guess
  // which one this screen wanted.
  if (roster_row_is_anon(roster_selection_)) {
    if (MenuSelect::is_left(key) || MenuSelect::is_right(key) ||
        MenuSelect::is_confirm(key)) {
      g_prefs.allow_anonymous = !g_prefs.allow_anonymous;
      NET_LOG("net: allow anonymous players: %s\n",
              g_prefs.allow_anonymous ? "YES" : "NO");
      save_preferences();  // a hosting policy should outlive the session
      return;
    }
  }
  if (roster_row_is_peer(roster_selection_) &&
      (MenuSelect::is_left(key) || MenuSelect::is_right(key))) {
    // BAN only where a ban would mean something (net_identity_bannable):
    // on a peer whose name the worker never vouched for, the key is their
    // own say-so and they can walk back in under another one. The row
    // draws KICK alone there, and this keeps the state matching the draw.
    roster_ban_ = MenuSelect::is_right(key) &&
                  roster_row_can_ban(roster_selection_);
    roster_kick_armed_ = -1;  // changing the action disarms
    return;
  }
  if (MenuSelect::is_confirm(key) && roster_row_is_peer(roster_selection_)) {
    if (roster_kick_armed_ == roster_selection_) {
      NetPeer *p = roster_peer_at(roster_selection_);
      roster_kick_armed_ = -1;
      if (p) net_kick_peer(*p, roster_ban_);
    } else {
      roster_kick_armed_ = roster_selection_;
    }
    return;
  }
  if (MenuSelect::is_back(key) && roster_kick_armed_ >= 0) {
    roster_kick_armed_ = -1;  // back disarms before it closes the screen
    return;
  }
  if (MenuSelect::is_back(key) || MenuSelect::is_confirm(key)) {
    roster_active_ = false;  // back to the pause menu
    return;
  }
  if (MenuSelect::move(key, roster_selection_, roster_row_count())) {
    roster_kick_armed_ = -1;  // moving off a row disarms it
    roster_ban_ = false;      // ...and the next row opens on the softer one
    return;
  }
  // Input re-binding is a LOCAL-seat idea: online the seats belong to
  // peers and the only actions are KICK and BAN (handled above).
  if (net_mode_ != NetOff) return;
  if (!MenuSelect::is_left(key) && !MenuSelect::is_right(key)) return;
  std::vector<SeatInput> options = roster_input_options();
  if (options.empty()) return;
  SeatInput current = roster_selection_ < (int)players->size()
                          ? roster_seat_input(roster_selection_)
                          : SeatInput();
  int at = 0;
  for (int i = 0; i < (int)options.size(); i++)
    if (options[i].same_as(current)) { at = i; break; }
  at += MenuSelect::is_right(key) ? 1 : -1;
  if (at < 0) at = (int)options.size() - 1;
  if (at >= (int)options.size()) at = 0;
  roster_apply(roster_selection_, options[at]);
}

bool GLGame::roster_claim_pad(SDL_JoystickID which) {
  // Offline only. Press-to-claim binds a LOCAL device to the highlighted
  // row, and online that row is a remote pilot's ship: the host's spare
  // pad would end up driving the peer's hull alongside their INPUT
  // stream. roster_nav's left/right was gated when the screen went
  // online; this second entry point into roster_apply was not.
  if (net_mode_ != NetOff) return false;
  if (is_player_controller(which)) return false;  // already driving a seat
  SeatInput in;
  in.kind = SeatInput::Pad;
  in.pad = which;
  roster_apply(roster_selection_, in);
  return true;
}

bool GLGame::is_player_controller(SDL_JoystickID which) const {
  for (auto *gs : *players)
    if (gs->wasMyController(which)) return true;
  return false;
}

// An opened pad that owns no seat must not carry run-ending authority
// (FOURPLAYER.md A4): GUIDE-pause, BACK-exit and the game-over confirms act
// only from a player's pad — or from any pad in a game where NO player has
// one (keyboard players with a couch pad, the long-shipped behaviour).
bool GLGame::pad_may_command(SDL_JoystickID which) const {
  if (is_player_controller(which)) return true;
  for (auto *gs : *players)
    if (gs->has_controller()) return false;
  return true;
}

bool GLGame::back_pressed() {
  // Online, back is not a quit — same rule as the Esc key and pad BACK:
  // it opens the pause screen (touch keeps its EXIT TO MENU band, the
  // cursor platforms their pause menu) and closes it again, and leaving
  // is the deliberate pick on that screen. Offline and at game over the
  // old direct exit stands.
  if ((net_mode_ == NetHost || net_mode_ == NetClient) &&
      !all_players_out()) {
    toggle_pause();
    return true;
  }
  save_progress();
  request_state_change(new Menu());
  return true;
}

void GLGame::focus_lost() {
  save_progress();
  // Replay checkpoint (REPLAY.md): background/focus-loss flush — on mobile
  // and Xbox this is the last chance before a possible suspend/kill, and
  // the level-boundary flushes keep this append small (at most one level
  // of records). toggle_pause below flushes too, but not when already
  // paused or online.
  if (replay_) replay_->flush();
  // Online the sim must keep running while unfocused — the peer's game
  // doesn't stop. Sound still mutes below.
  if(running && net_mode_ == NetOff) {
    toggle_pause();
    // toggle_pause refuses when the game is over — only remember an
    // auto-pause that actually took, or focus regain would pause a
    // running game-over screen.
    auto_paused = !running;
  }
  // Everything goes silent while the window is unfocused, pause tune included.
  if(pause_music_channel >= 0) Mix_Pause(pause_music_channel);
  Mix_PauseMusic();
}

void GLGame::controller_added(SDL_GameController *ctrl) {
  SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctrl));
  // Online, the only ship this machine controls is the local player;
  // players->front() is the remote host's ghost on a client (and the
  // remote client's ghost on a host), so binding "the first ship without
  // a controller" would hand the pad to a ship this machine can't drive.
  if (net_mode_ != NetOff) {
    GLShip *local = local_player();
    if (local && !local->is_my_controller_id(id) && !local->has_controller())
      local->set_controller(ctrl);
    return;
  }
  // Skip if any player already has this controller
  for(auto* glship : *players) {
    if(glship->is_my_controller_id(id)) return;
  }
  // Give it back to a seat that has NO input at all — a pad-joined seat
  // whose pad dropped, which is the reconnect case this exists for. It used
  // to take the first seat without a CONTROLLER, which on a keyboard game is
  // player 1: plugging a pad in (or launching with one already connected)
  // silently glued it onto the keyboard pilot, so two people drove one ship
  // and the pad's owner could never claim a seat of their own — the blocker
  // for 2 keyboard + 2 pad (field, 2026-08-11). A spare pad now stays free,
  // which is what START-to-join and the seat roster both want.
  for(auto* glship : *players) {
    if(!glship->has_controller() && !glship->has_keys()) {
      glship->set_controller(ctrl);
      return;
    }
  }
}

bool GLGame::has_free_controller() const {
  int n = SDL_NumJoysticks();
  for(int i = 0; i < n; i++) {
    if(!SDL_IsGameController(i)) continue;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
    bool assigned = false;
    for(auto* glship : *players) {
      if(glship->is_my_controller_id(id)) { assigned = true; break; }
    }
    if(!assigned) return true;
  }
  return false;
}

void GLGame::controller_removed(SDL_JoystickID id) {
  for(auto* glship : *players) {
    if(glship->is_my_controller_id(id)) {
      glship->set_controller(NULL);
      // Don't pause for a player who is already game over (dead, no lives):
      // in two-player their disconnect must not interrupt the survivor.
      bool player_game_over = !glship->ship->is_alive() && glship->ship->lives == 0;
      if(running && !player_game_over) toggle_pause();
      return;
    }
  }
}

// The one local-join path (FOURPLAYER.md D6): pad joins pass the pad and no
// keyboard bindings (the pad is the controls), the Enter join passes
// with_keys so the new seat's PlayerKeys slot binds. Gated on
// LOCAL_PLAYER_CAP, not MAX_PLAYERS — the dark-launch rule (FOURPLAYER.md
// §3); bypass_cap is the NEWTONIA_START_PLAYERS test hook's door.
void GLGame::add_local_player(SDL_GameController *ctrl, bool with_keys,
                              bool bypass_cap) {
  if(net_mode_ != NetOff) return;  // extra seats online are Phase B
  if(!bypass_cap && (int)players->size() >= LOCAL_PLAYER_CAP) return;
  if((int)players->size() >= MAX_PLAYERS) return;
  Ship* p1 = players->front()->ship;
  if(!p1->is_alive() && !p1->lives) return;
  GLShip* object = make_seat_ship(grid, (int)players->size());
  set_player_keys(object, (int)players->size(), /*with_bindings=*/with_keys);
  if(ctrl != NULL) object->set_controller(ctrl);
  object->ship->is_local_player = true;
  object->ship->set_missile_asteroids((std::list<Object*>*)objects);
  ship_objects->push_back(object->ship);
  for(auto *p : *players) p->ship->set_missile_ships(ship_objects);
  object->ship->set_missile_ships(ship_objects);
  object->ship->missiles_seek_players = friendly_fire;
  object->ship->set_shock_targets(shock_targets);
  object->ship->set_black_holes(black_holes);
  players->push_back(object);
  update_presence();
}

void GLGame::update_presence() const {
  // Displayed level numbers = internal generation + 1, the same rule as
  // achievements (ACHIEVEMENTS.md §5).
  Presence::set_level(generation + 1, (int)players->size());
}

// A remote pilot's hull, host side: same wiring as add_local_player but
// with no local controller or key bindings — the peer drives it via INPUT
// messages. Hull/tint by seat (make_seat_ship, the D7 rule) so a 4P
// host's screen matches every client's: the hardcoded plain GLCar here
// dressed seats 3-4 as a second and third P2 (the client and replay
// paths already seated hulls correctly — the host was the odd one out).
// At 2P this is byte-identical: seat 2's tint IS the GLCar default.
void GLGame::add_remote_player(uint8_t seat) {
  if((int)players->size() >= net_seat_cap()) return;
  int wire_seat = seat ? (int)seat : (int)players->size() + 1;
  GLShip* object = make_seat_ship(grid, wire_seat - 1);
  object->ship->set_missile_asteroids((std::list<Object*>*)objects);
  ship_objects->push_back(object->ship);
  for(auto *p : *players) p->ship->set_missile_ships(ship_objects);
  object->ship->set_missile_ships(ship_objects);
  object->ship->missiles_seek_players = friendly_fire;
  object->ship->set_black_holes(black_holes);
  players->push_back(object);
  // The Ship constructor creates ships dead (offline player 2 waits out
  // the respawn countdown after pressing Enter to join mid-game). The
  // remote player just finished the whole lobby flow — bring them up
  // alive immediately, with the usual spawn-invincibility window.
  object->ship->respawn(grid, false);
  // Keep the join spawn clear of player 1: safe_position() only avoids
  // asteroids, and a nearby (or overlapping) spawn is lethal with
  // friendly fire on — respawn's detonate() flash is made of real
  // bullets, and body overlap kills outright.
  Ship *p1_ship = players->front()->ship;
  for (int tries = 0; tries < 32; tries++) {
    // ("near" is a reserved legacy macro in the Windows headers)
    Point nearest = object->ship->position.closest_to(p1_ship->position);
    if ((nearest - p1_ship->position).magnitude() > 400.0f) break;
    object->ship->position = WrappedPoint();
    object->ship->safe_position(grid, false);
  }
  object->ship->bullets.clear();  // drop the lethal spawn-flash debris
  // PROTO 14: this ship's bullets arrive as MSG_SHOT reports — its own
  // gun sim keeps its bookkeeping but mints no bullets.
  object->ship->net_remote_gun = true;
  // PROTO 17: mirror of the client's shot reporting — every shot the host
  // player fires is echoed to the client as MSG_SHOT (drained in tick()),
  // which spawns an exact clone instantly instead of waiting for the next
  // 10 Hz snapshot rebuild.
  p1_ship->net_report_shots = true;
  // PB-D2: any join forces a GLOBAL keyframe slot — the newcomer has no
  // delta baseline, and per-peer baselines are a non-starter (the replay
  // recorder tees the one shared build).
  net_force_keyframe_ = true;
  update_presence();  // rich presence: now a co-op game
}

// Elastic asteroid-asteroid collisions: 2D impulse physics (mass ~
// radius^2) plus a positional push that resolves overlap, for every live
// pair where at least one is elastic (reflective asteroids carry
// elastic=true). Pairs are processed once via inner iterator starting
// after outer. ONE definition for two callers: the host simulates it for
// real (announce=true: bounce ting + EV_ROID_BOUNCE_AT), and the net client
// mirrors it silently each visual step — bounces are position-and-
// pairing-dependent, so without the mirror every one of them was a
// surprise the authoritative records corrected 100 ms later (the last
// source of asteroid jitter; gravity is mirrored the same way).
// Resolve one elastic asteroid pair (at least one of a/b is elastic):
// separation push, mass-proportional impulse, and the visible-bounce
// ting. ONE definition for both scan paths in
// elastic_asteroid_collisions, so the shrunk inner loop cannot drift
// from the full one.
void GLGame::collide_elastic_pair(Asteroid *a, Asteroid *b, bool announce) {
    // Use world-wrap aware distance: get closest copy of A to B
    Point a_near = a->position.closest_to(b->position);
    float dx = a_near.x() - b->position.x();
    float dy = a_near.y() - b->position.y();
    float dist2 = dx * dx + dy * dy;
    float sum_r = a->radius + b->radius;
    if(dist2 >= sum_r * sum_r) return; // no overlap

    float dist = sqrtf(dist2);
    if(dist < 1e-4f) return; // degenerate overlap, skip

    // Collision normal pointing from B to A
    float nx = dx / dist;
    float ny = dy / dist;

    // Positional correction: push apart to resolve overlap, including
    // children spawning inside each other. Proportional ONLY — a flat
    // +0.5/tick bias made host and client (which mirrors this pass)
    // diverge at up to ~60 units/s whenever they disagreed about a
    // borderline contact, since the bias fires at any overlap depth.
    float overlap = sum_r - dist;
    float push = overlap * 0.6f;
    a->position += Point(nx, ny) * push;
    a->position.wrap();
    b->position += Point(-nx, -ny) * push;
    b->position.wrap();

    // Velocity impulse: only when approaching (negative = approaching)
    float vrel_n = (a->velocity.x() - b->velocity.x()) * nx
                 + (a->velocity.y() - b->velocity.y()) * ny;
    if(vrel_n >= 0.0f) return; // already separating, no impulse needed

    // Mass proportional to area (radius^2)
    float ma = a->radius * a->radius;
    float mb = b->radius * b->radius;
    float impulse = -2.0f * vrel_n * ma * mb / (ma + mb);

    a->velocity = a->velocity + Point(nx, ny) * (impulse / ma);
    b->velocity = b->velocity - Point(nx, ny) * (impulse / mb);

    // Play a deep metallic ting when an asteroid strikes a reflective one,
    // but only if the collision is visible to any player — and only as
    // loud as the hit was hard. -vrel_n is the closing speed along the
    // contact normal (vrel_n < 0 here, or we returned above): a full ring
    // needs an honest impact (~a fast rock's whole speed, max_speed/radius
    // tops out at 0.3), and below the floor the cue is skipped entirely.
    // Without the scaling, a crowd of elastic rocks jostling at near-zero
    // relative speed — the normal state of a late generation, where a
    // dozen of them share the level — rang every graze like a hammer blow
    // (Glenn's level 25). The base rides the wire too, so the client
    // hears the same hardness.
    static const float kBounceRingSpeed = 0.15f;  // closing speed of a full ring
    static const float kBounceMinBase   = 0.25f;  // quieter than this: skip
    float ring_base = -vrel_n / kBounceRingSpeed;
    if(ring_base > 1.0f) ring_base = 1.0f;
    if(announce && ring_base >= kBounceMinBase &&
       (a->reflective || b->reflective) &&
       Asteroid::asteroid_ting_sound != NULL) {
      Point contact(
        (a->position.x() + b->position.x()) * 0.5f,
        (a->position.y() + b->position.y()) * 0.5f);
      // Audible to ANYONE seated (players holds the remote replicas too):
      // worth a local play and a wire event. WorldSound attenuates the
      // local play against THIS machine's camera; the client re-attenuates
      // the event against its own (EV_ROID_BOUNCE_AT) — the old event
      // carried this max-over-listeners volume as the playback level, so
      // a bounce beside the host rang at full volume on a client parked
      // across the world.
      float vol = sound_volume_for_point(contact);
      if(vol > 0.0f) {
        // Two limits, different jobs. The global cadence cap bounds how
        // often the WORLD can ring, whatever the population: the pass
        // fires for elastic-vs-ANY pairs, so a late generation's dozen
        // reflective rocks plough through hundreds of ordinary ones and
        // per-collision gates alone (the impulse floor, the per-rock
        // refractory below) still let a cluster ring several times a
        // second — the cap makes the ting a sparse texture at any
        // density. The per-ROCK refractory then keeps the sparse rings
        // honest: a rock that just rang stays quiet for a couple of
        // seconds, so the cadence budget goes to fresh collisions
        // instead of one jostling pair. Stamped on both partners.
        static const Uint32 kBounceGlobalCadenceMs = 800;
        static const Uint32 kRockRingRefractoryMs = 2000;
        static Uint32 last_asteroid_ting_tick = UINT32_MAX;
        Uint32 now = SDL_GetTicks();
        if(now - last_asteroid_ting_tick >= kBounceGlobalCadenceMs &&
           now - a->last_bounce_ring >= kRockRingRefractoryMs &&
           now - b->last_bounce_ring >= kRockRingRefractoryMs) {
          last_asteroid_ting_tick = now;
          a->last_bounce_ring = b->last_bounce_ring = now;
          WorldSound::play(Asteroid::asteroid_ting_sound, contact, ring_base);
          net_send_event(Net::EV_ROID_BOUNCE_AT,
                         Net::pack_pos_vol(contact.x(), contact.y(), ring_base,
                                           world.x(), world.y()));
        }
      }
    }
}

void GLGame::elastic_asteroid_collisions(bool announce) {
  // Only pairs with at least one elastic member can do anything, and
  // elastic asteroids are RARE: num_reflective is (generation-2)/2+1, so
  // generation 25 has 12 of them among 321 asteroids. Visiting all
  // n(n-1)/2 pairs to discard ~93% of them cost 120 ms/s of a 145 ms/s
  // tick budget on desktop at that generation — and the whole frame on a
  // low-end phone (field: Moto G05, 2-5 fps at gen 25, tick ~1050 ms/s).
  //
  // So shrink the INNER loop, not the outer one. Walking the outer list
  // unchanged and scanning only the elastic asteroids after a non-elastic
  // one yields the exact same pairs in the exact same ORDER — which
  // matters: the positional correction below mutates positions as it
  // goes, so a later pair's overlap test can depend on an earlier
  // correction. Iterating elastics as the outer loop instead would
  // reorder every (non-elastic, elastic) pair, and the net client mirrors
  // this pass step for step.
  std::vector<Asteroid*> elastics, by_index;
  std::unordered_map<const Object*, int> order;
  by_index.reserve(objects->size());
  order.reserve(objects->size() * 2);
  for(Asteroid *a : *objects) {
    order[a] = (int)by_index.size();
    by_index.push_back(a);
    if(a->alive && a->elastic) elastics.push_back(a);
  }
  if(elastics.empty()) return;

  // NOT "near": that is a legacy segment-qualifier macro the Windows
  // headers still define, so a local of that name miscompiles under MinGW
  // with an error naming neither the variable nor the real cause (CI,
  // 2026-07-28). Same trap as the `nearest` note further up this file.
  std::vector<Object*> nearby;
  std::vector<int> cand;
  std::list<Asteroid*>::iterator ai;
  size_t ei = 0;  // elastics[ei..] are the elastic asteroids AFTER *ai
  for(ai = objects->begin(); ai != objects->end(); ++ai) {
    Asteroid *a = *ai;
    if(ei < elastics.size() && elastics[ei] == a) ++ei;
    if(!a->alive) continue;

    // Non-elastic outer: its only possible partners are the elastics still
    // ahead of it, and there are a handful of those — no grid needed.
    if(!a->elastic) {
      for(size_t k = ei; k < elastics.size(); ++k) {
        Asteroid *b = elastics[k];
        if(b->alive) collide_elastic_pair(a, b, announce);
      }
      continue;
    }

    // Elastic outer: only rocks sharing a cell (or the +/-1 ring) can be
    // within sum_r, because cells are 2*max_radius across and update()
    // files a body into every cell it overlaps — the same guarantee
    // Grid::collide() relies on. Sorting the candidates back into list
    // order keeps the pair sequence identical to the old all-pairs scan,
    // which matters because the separation push below mutates positions
    // as it goes.
    grid.query_neighbours(*a, nearby);
    int a_idx = order[a];
    cand.clear();
    for(size_t k = 0; k < nearby.size(); ++k) {
      std::unordered_map<const Object*, int>::const_iterator f =
          order.find(nearby[k]);
      if(f == order.end()) continue;   // not one of this list's asteroids
      if(f->second <= a_idx) continue; // pair each combination once only
      cand.push_back(f->second);
    }
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
    for(size_t k = 0; k < cand.size(); ++k) {
      Asteroid *b = by_index[cand[k]];
      if(b->alive) collide_elastic_pair(a, b, announce);
    }
  }
}

// Quantum observation: collapse when any player looks at a quantum
// asteroid (base speed), superposition otherwise (4x speed so it can
// sneak up on players who look away). ONE definition for two callers:
// the host simulates it for real, and the net client mirrors it every
// visual step — both ships' poses and facings are known here, and
// without the mirror every observation flip changed the rock's speed
// 4x on the host only, so the client extrapolated at the wrong speed
// until the next record: rocks "warping back and forth and ending up
// in their expected location". The state-byte in the records remains
// authoritative; this only keeps the between-record extrapolation right.
void GLGame::update_quantum_observation() {
  for(std::list<Asteroid*>::iterator oi = objects->begin();
      oi != objects->end(); ++oi) {
    Asteroid *ast = *oi;
    if(!ast->quantum) continue;
    bool now_observed = is_point_faced_by_any_player(ast->position);
    if(now_observed == ast->quantum_observed) continue;
    ast->quantum_observed = now_observed;
    float spd = ast->velocity.magnitude();
    if(spd > 1e-6f) {
      Point dir = ast->velocity * (1.0f / spd);
      if(now_observed) {
        // Collapse: slow to base speed
        ast->velocity = dir * ast->quantum_base_speed;
      } else {
        // Superposition: speed up 4x
        ast->velocity = dir * ast->quantum_base_speed * 4.0f;
      }
    }
  }
}

// ---- RTT probe (PROTO 11) ----------------------------------------------
// 1 Hz MSG_PING on the UNRELIABLE channel; the peer echoes the timestamp
// back as MSG_PONG untouched, so only the sender's clock is ever read —
// no cross-machine clock comparison. A lost probe just skips a sample.

void GLGame::net_ping_tick(int delta) {
  // B5: every live peer gets its own 1 Hz probe on its own timer — a
  // seat-3 RTT read off seat-2's link would be fiction.
  for (NetPeer *pr : net_peers_) {
    if (!pr->session || pr->lost) continue;
    pr->ping_timer += delta;
    if (pr->ping_timer < 1000) continue;
    pr->ping_timer = 0;
    // Piggybacked 1 Hz sample: at these traffic rates the transport send
    // buffer drains in microseconds, so ANY nonzero here means our sender
    // is blocked — the smoking gun that an outage is the path/relay eating
    // the flow (peer stopped acking) rather than in-flight delay.
    if (Net::net_debug_enabled()) {
      int buffered = pr->session->transport()->buffered_amount();
      if (buffered > 0) NET_LOG("net: tx buffered %d bytes\n", buffered);
    }
    std::vector<uint8_t> p;
    Net::put_header(p, Net::MSG_PING, (uint8_t)net_local_seat());
    Net::put_u32(p, (uint32_t)SDL_GetTicks());
    pr->session->transport()->send_unreliable(&p[0], p.size());
  }
}

bool GLGame::net_handle_ping_pong(uint8_t msg_type, Net::Reader &r,
                                  NetPeer *from) {
  // B5: the counterparty is the peer whose transport delivered the frame
  // (host drain passes it); the client's sole peer is the host.
  NetPeer &peer = from ? *from : net_peer_make();
  if (msg_type == Net::MSG_PING) {
    uint32_t t = r.u32();
    if (!r.ok) return true;
    std::vector<uint8_t> p;
    Net::put_header(p, Net::MSG_PONG, (uint8_t)net_local_seat());
    Net::put_u32(p, t);
    if (peer.session)
      peer.session->transport()->send_unreliable(&p[0], p.size());
    return true;
  }
  if (msg_type == Net::MSG_PONG) {
    uint32_t t = r.u32();
    if (!r.ok) return true;
    // uint32 subtraction survives SDL_GetTicks wrap; anything over 10 s
    // is a thawed process or a stalled relay, not a latency reading.
    float sample = (float)(uint32_t)((uint32_t)SDL_GetTicks() - t);
    if (sample < 10000.0f) {
      if (peer.rtt_ms < 0.0f) NET_LOG("net: rtt %.0f ms (first pong)\n", sample);
      peer.rtt_ms = peer.rtt_ms < 0.0f ? sample
                                       : peer.rtt_ms * 0.8f + sample * 0.2f;
      peer.rtt_ring[peer.rtt_ring_i] = sample;
      peer.rtt_ring_i = (peer.rtt_ring_i + 1) % 8;
      if (peer.rtt_ring_n < 8) peer.rtt_ring_n++;
    }
    return true;
  }
  return false;
}

float GLGame::net_lead_ms() const {
  // Minimum of the recent samples, not the smoothed average: a spike is
  // relay queueing, not path length, and the smoothed value stays wrong
  // for ~10 s after one while every extrapolation target overshoots.
  NetPeer *p = net_peer();
  if (!p || p->rtt_ring_n == 0) return 0.0f;
  float rtt = p->rtt_ring[0];
  for (int i = 1; i < p->rtt_ring_n; i++)
    if (p->rtt_ring[i] < rtt) rtt = p->rtt_ring[i];
  float lead = rtt * 0.5f;
  return lead > 250.0f ? 250.0f : lead;
}

// World objects (asteroids, the remote ship) are extrapolated locally with
// the same physics the host runs — but host-side collision responses
// (elastic bounces, asteroid-vs-reflective deflections) are unpredictable
// here, so authoritative records routinely land 30-100 units from the
// local pose. Overwriting yanked objects at 10 Hz, and partial blending
// made them wobble to-and-fro ("looks like out-of-order states" — it
// wasn't; the rel channel is ordered). So: adopt the authoritative
// VELOCITY exactly (the simulation stays right), keep the RENDER pose
// where it is, and bank the difference in net_pose_err, which
// net_smooth_step drains over ~150 ms — corrections become a glide in
// one direction. Teleport-scale jumps still snap: they should look
// instant. Returns the pre-correction error distance (diagnostics).
float GLGame::net_reconcile_pose(Object &o, const WrappedPoint &old_render,
                                 bool sim_exact) const {
  float lead = net_lead_ms();
  WrappedPoint target(o.position.x() + o.velocity.x() * lead,
                      o.position.y() + o.velocity.y() * lead);
  target.wrap();
  Point c = target.closest_to(old_render);
  float cx = c.x() - old_render.x();
  float cy = c.y() - old_render.y();
  float err2 = cx * cx + cy * cy;
  // Asteroids get a wider glide budget: after a connection stall the
  // near-hole rocks legitimately curve 300-700 units away from the blind
  // extrapolation, and the recovery burst should swoosh in, not teleport
  // a dozen rocks in one frame. Real asteroid teleports land at a random
  // spot in a 2950+ world — far beyond 800 essentially always.
  const float snap_dist = sim_exact ? 800.0f : 250.0f;
  // 1 Hz summary of how hard the incoming authority fights the local
  // extrapolation — the number that says whether visible jitter is
  // network correction (big counts / big max) or something else (silence).
  if (Net::net_debug_enabled()) {
    static uint32_t s_last = 0;
    static int s_n = 0, s_snaps = 0;
    static float s_max = 0.0f;
    float err = sqrtf(err2);
    if (err >= 8.0f) {
      s_n++;
      if (err > s_max) s_max = err;
      if (err >= snap_dist) s_snaps++;
    }
    uint32_t now = SDL_GetTicks();
    if (now - s_last >= 1000) {
      if (s_n)
        NET_LOG("net: reconcile %d corrections/s (max %.0f units, %d snaps)\n",
                s_n, s_max, s_snaps);
      s_last = now;
      s_n = 0;
      s_snaps = 0;
      s_max = 0.0f;
    }
  }
  if (sim_exact) {
    // Asteroids: sim = authority NOW (gravity mirrors correctly again);
    // the render offset preserves what the player saw at this instant
    // and fades to truth (~150 ms) — or is dropped whole on a
    // teleport-scale jump, which should look instant.
    o.position = target;
    o.net_pose_err = err2 < snap_dist * snap_dist ? Point(-cx, -cy)
                                                  : Point(0.0f, 0.0f);
  } else if (err2 < snap_dist * snap_dist) {
    o.position = old_render;
    o.net_pose_err = Point(cx, cy);  // replaces (not adds to) the old debt
  } else {
    o.position = target;
    o.net_pose_err = Point(0.0f, 0.0f);
  }
  o.position.wrap();
  return sqrtf(err2);
}

namespace {
// Drain a client object's banked authoritative correction: exponential
// decay with a ~65 ms time constant (~150 ms to mostly gone), applied
// every visual step so the glide is frame-smooth.
void net_smooth_step(Object &o, int delta) {
  float ex = o.net_pose_err.x(), ey = o.net_pose_err.y();
  if (ex * ex + ey * ey < 0.25f) {
    o.net_pose_err = Point(0.0f, 0.0f);
    return;
  }
  float k = 1.0f - expf(-(float)delta / 65.0f);
  Point c = o.net_pose_err * k;
  o.position += c;
  o.position.wrap();
  o.net_pose_err = o.net_pose_err - c;
}

// How fast a replicated ship turns relative to the real one while the
// client extrapolates between snapshots (Ship::net_rotation_damp). At 1.0
// a turn that ends just after a snapshot overshoots by a full interval —
// 100 ms at 286 deg/s is 29 degrees — and the reconcile visibly unwinds
// it: "rotates a bit further, then un-rotates" at the end of every turn
// (field, 2026-07-29). Below 1.0 the ship instead runs slightly BEHIND
// authority, so each correction points the way it is already turning and
// disappears into the motion. Lower = less overshoot, more lag; 0.6 keeps
// a sustained turn tracking within a few degrees while cutting the
// end-of-turn reversal by 40%.
static const float NET_ROTATION_DAMP = 0.6f;

// Facing twin of net_reconcile_pose, and for the same reason. restore_state
// hard-assigns the authoritative facing every apply, while a replicated
// ship's rotation only extrapolates when a snapshot happens to catch
// rotation_direction set. So a turn's first ~100 ms of rotation arrives in
// ONE step: measured at +25 deg in a single 8 ms step on a replay ghost,
// against a dead-constant 2.29 deg/step through the sustained part of the
// same turn (2026-07-27) — and every start/stop of a turn does it, which is
// why it reads as continuous jerkiness rather than an occasional jump. Bank
// the difference and keep the drawn facing, exactly as net_pose_err does for
// position. Big flips still snap: a teleport or respawn should look instant,
// not swing round.
void net_reconcile_facing(Ship &s, const Point &old_facing) {
  // Signed angle old->authoritative, in radians (Point::rotate's unit).
  float dot = old_facing.x() * s.facing.x() + old_facing.y() * s.facing.y();
  float crs = old_facing.x() * s.facing.y() - old_facing.y() * s.facing.x();
  float d = atan2f(crs, dot);
  if (fabsf(d) > 1.5f) {  // ~86 deg: not a turn, a flip — let it snap
    s.net_facing_err = 0.0f;
    return;
  }
  s.facing = old_facing;
  s.net_facing_err = d;
}

// Drain it on net_smooth_step's time constant so a ship's rotation and its
// position corrections glide together rather than at different rates.
void net_smooth_facing(Ship &s, int delta) {
  if (fabsf(s.net_facing_err) < 0.0005f) {
    s.net_facing_err = 0.0f;
    return;
  }
  float c = s.net_facing_err * (1.0f - expf(-(float)delta / 65.0f));
  s.facing.rotate(c);
  s.net_facing_err -= c;
}

// Asteroid flavour (sim_exact reconcile): the sim pose already rides the
// authority — only the drawn-continuity offset fades. Exponential for
// small offsets (invisible), RATE-CAPPED for big ones: a post-gap burst
// banks 100+ units on every rock at once, and a fixed ~150 ms drain
// moved the entire field at several times its natural speed — a
// coordinated lurch that reads as jitter no matter how smooth each
// individual glide is. Capped near the rock's own speed the correction
// hides inside ordinary motion (a 150-unit debt takes ~1 s to melt).
// PROXIMITY-SCALED: rocks near the player drain at full exponential
// speed — anything that can kill you is drawn where it truly is (a
// lingering banked offset near the ship meant "asteroids kill me when I
// didn't see the impact") — while distant rocks keep the rate-capped
// crawl that stops the whole-field lurch.
void net_decay_render_offset(Object &o, int delta,
                             const WrappedPoint &player) {
  float ex = o.net_pose_err.x(), ey = o.net_pose_err.y();
  float off2 = ex * ex + ey * ey;
  if (off2 < 0.25f) {
    o.net_pose_err = Point(0.0f, 0.0f);
    return;
  }
  float off = sqrtf(off2);
  float want = off * (1.0f - expf(-(float)delta / 65.0f));
  Point near_p = o.position.closest_to(player);
  float dx = near_p.x() - player.x(), dy = near_p.y() - player.y();
  if (dx * dx + dy * dy > 700.0f * 700.0f) {
    float cap = (o.velocity.magnitude() * 1.5f + 0.25f) * (float)delta;
    if (want > cap) want = cap;
  }
  o.net_pose_err = o.net_pose_err * ((off - want) / off);
}
}  // namespace

// B4: one peer's full drain — INPUT decode, claims, effects — against its
// own transport, budgets, and seat's ship. net_host_poll() below loops it.
void GLGame::net_host_poll_peer(NetPeer &peer) {
  NetTransport *t = peer.session->transport();
  GLShip *remote_gs = player_by_seat(peer.seat);
  Ship *remote = remote_gs ? remote_gs->ship : NULL;
  // PROTO 25: a peer's effect messages (SHOT/LANCE/SHOCK) name the firing
  // seat in the header. Resolve it — bounded to REMOTE seats only (2..),
  // so a bad byte can never point a peer's effects at the host's own ship.
  // At one peer this is exactly the old `remote`; at B4 it picks the
  // sending peer's ship out of the roster.
  auto firer_by_header = [&](const Net::Header &h) -> Ship * {
    if (h.player_id < 2) return remote;  // legacy-shaped byte: old behaviour
    GLShip *gs = player_by_seat(h.player_id);
    return gs ? gs->ship : remote;
  };

  // Mid-gap marker (host side): fires once when INPUT silence crosses
  // 300 ms, with our send-buffer depth at that instant — pairs with the
  // client's "rx quiet" line to show whether both senders were blocked.
  if (Net::net_debug_enabled() && running && peer.have_input) {
    int quiet = current_time - peer.last_input_time;
    if (quiet > 300 && !peer.quiet_logged) {
      peer.quiet_logged = true;
      NET_LOG("net: input quiet 300 ms, tx buffered %d bytes\n",
              t->buffered_amount());
    } else if (quiet <= 300) {
      peer.quiet_logged = false;
    }
  }

  // Close an input-gap observation window (opened below) and report what
  // the gap turned out to be — see glgame.h net_gap_* for how to read it.
  if (peer.gap_deadline && current_time >= peer.gap_deadline) {
    NET_LOG("net: post-gap 1.5 s: %d inputs accepted (~185 normal), "
            "%d stale rx, max seq leap %u (skipped at gap end: %u)\n",
            peer.gap_accepts, peer.gap_stragglers, peer.gap_max_leap,
            peer.gap_skipped);
    peer.gap_deadline = 0;
  }

  // Flood guard (safety, not anti-cheat): a peer that spams the spawn/claim
  // channels would otherwise make us allocate (bullets/lance_pulses/shocks)
  // and work without bound in this single drain. Two budgets per poll: a
  // total read-loop cap so a message storm can't spin here, and a shared
  // action budget across the droppable families so per-frame allocation
  // stays bounded. A legitimate client never approaches either (a busy frame
  // is a handful of shots + one INPUT); over budget we drop the effect and
  // keep draining. NEVER gated: INPUT (the pose/control stream — cheap, and
  // the read-loop cap bounds it) and EVENT (reliable+ordered and stateful —
  // a dropped EV_PAUSE/EV_BYE/EV_PICKUP is consumed from SCTP, never
  // redelivered, and no snapshot reconciles it; a post-stall backlog can
  // legitimately exceed the budget in one drain. EVENT's two heavy codes are
  // bounded separately — see net_event_effect_budget_). PING stays counted:
  // that bounds the pong reflection, at worst staling the RTT lead until
  // the flood ends.
  const int NET_MAX_MSGS_PER_POLL = 512;    // read-loop bound
  const int NET_MAX_ACTIONS_PER_POLL = 64;  // spawn/claim effects acted on
  int net_msgs_seen = 0, net_actions = 0;
  bool net_action_cap_logged = false;
  net_event_effect_budget_ = NET_EVENT_EFFECTS_PER_POLL;

  std::vector<unsigned char> msg;
  // PB-D4: a peer's accepted effect frames (SHOT/LANCE/SHOCK) are relayed
  // verbatim to every OTHER live peer -- the PROTO 25 header already names
  // the firing seat, so receivers resolve the firer with no re-stamp.
  // No-op at one peer.
  auto relay_others = [this, &peer, &msg]() {
    for (NetPeer *op : net_peers_)
      if (op != &peer && op->session && !op->lost)
        op->session->transport()->send_reliable(&msg[0], msg.size());
  };
  while (t->poll(msg)) {
    if (++net_msgs_seen > NET_MAX_MSGS_PER_POLL) {
      NET_LOG("net: poll message storm - %d msgs, deferring rest\n",
              net_msgs_seen);
      break;
    }
    Net::Reader r(msg.empty() ? nullptr : &msg[0], msg.size());
    Net::Header h;
    if (!Net::read_header(r, h)) continue;
    if (h.msg_type != Net::MSG_INPUT && h.msg_type != Net::MSG_EVENT &&
        ++net_actions > NET_MAX_ACTIONS_PER_POLL) {
      if (!net_action_cap_logged) {
        NET_LOG("net: action budget %d/poll hit - dropping flood\n",
                NET_MAX_ACTIONS_PER_POLL);
        net_action_cap_logged = true;
      }
      continue;
    }
    if (net_handle_ping_pong(h.msg_type, r, &peer)) continue;
    if (h.msg_type == Net::MSG_EVENT) {
      uint8_t code = r.u8();
      uint32_t arg = r.remaining() >= 4 ? r.u32() : 0;
      if (r.ok) {
        // Replay tee (online recording): events received from the peer are
        // part of this machine's stream too — same skip list as the send
        // tee in net_send_event.
        if (replay_ && code != Net::EV_PAUSE && code != Net::EV_RESUME &&
            code != Net::EV_BYE && code != Net::EV_ACHIEVEMENT)
          replay_->record_event(code, arg);
        net_handle_event(code, arg, &peer);
      }
      continue;
    }
    if (h.msg_type == Net::MSG_SHOT) {
      // Client shot report (PROTO 14): spawn an exact clone of the
      // bullet the client fired — same spawn point, same spread-applied
      // velocity, same id. The remote ship's own gun sim mints nothing
      // (net_remote_gun); this is the only source of its bullets.
      uint32_t id = r.u32();
      float sx = r.f32(), sy = r.f32(), svx = r.f32(), svy = r.f32();
      uint8_t flags = r.u8();
      Ship *firer = firer_by_header(h);
      if (!r.ok || !firer) continue;
      if (std::isfinite(sx) && std::isfinite(sy) && std::isfinite(svx) &&
          std::isfinite(svy) && svx * svx + svy * svy < 25.0f) {
        firer->net_spawn_reported_bullet(id, Point(sx, sy), Point(svx, svy),
                                         (flags & 1) != 0, (flags & 2) != 0,
                                         (flags & 4) != 0);  // PROTO 18 pierce
        relay_others();  // PB-D4
        // Replay tee: the remote gun's sound cue — its bullets ride the
        // snapshots, but the pew never would (the outbox tee is suppressed
        // with the rest of the remote gun sim).
        replay_record_shot(sx, sy, (flags & 4) ? 1 : 0);
      }
      continue;
    }
    if (h.msg_type == Net::MSG_LANCE) {
      // Client lance pulse (PROTO 18): push the traced polyline onto the
      // remote ship for the flash (asteroid kills arrive separately as
      // bullet_id-0 MSG_HIT claims), then resolve its SHIP/station hits
      // here — ship damage is host authority, and the polyline is already
      // on the wire, so no extra message is needed. The size guard skips
      // resolution if the receive bailed without pushing a pulse.
      Ship *firer = firer_by_header(h);
      if (!firer) continue;
      size_t before = firer->lance_pulses.size();
      if (net_receive_lance_pulse(r, firer) &&
          firer->lance_pulses.size() > before) {
        replay_record_polyline(Replay::FX_LANCE, firer,
                               firer->lance_pulses.back().points);
        resolve_lance_ship_hits(firer, firer->lance_pulses.back().points);
        net_resolve_polyline_block(firer->lance_pulses.back().points);
        relay_others();  // PB-D4
      }
      continue;
    }
    if (h.msg_type == Net::MSG_SHOCK) {
      // Client shock bolt (PROTO 22): show the exact polyline on the remote
      // ship + zap sound, then apply station/mini HULL damage from it host-
      // side (enemy kills arrive as bullet_id-0 MSG_HIT_SHIP claims, asteroids
      // as MSG_HIT). One hull hit per bolt, like a bullet.
      Ship *firer = firer_by_header(h);
      if (!firer) continue;
      std::vector<Point> pts;
      if (!net_receive_shock_pulse(r, firer, &pts)) continue;
      replay_record_polyline(Replay::FX_SHOCK, firer, pts);
      // The bolt's blocked endpoint (teleport evade / tough chip / ghost
      // feedback) is the host's call — the client stopped its arc without
      // claiming (offline-consistency, Glenn 2026-08-02).
      net_resolve_polyline_block(pts);
      if (station != NULL && station->is_alive() &&
          shock_bolt_reaches(pts, station->position, station->radius)) {
        station->hit();
        if (!station->is_alive() && firer->is_local_player)
          Achievements::unlock("station_destroyed");
      }
      if (mini_station != NULL && mini_station->is_alive() &&
          shock_bolt_reaches(pts, mini_station->position, mini_station->radius)) {
        firer->explode(mini_station->position, mini_station->velocity);
        firer->score += GLMiniStation::REWARD;
        mini_station->destroy();
        if (station_explode_sound != NULL)
          Mix_PlayChannel(-1, station_explode_sound, 0);
      }
      // Mid-game hazards the client's arc reached (the polyline already ends
      // where its bolt stopped, so this only touches ones it truly hit): same
      // per-kind damage the host's own bolt applies in tick() — a seeker dies
      // outright, a comet sheds chunks, a comet/pulsar pays out on break-up.
      // The client showed the arc/spark; destruction replicates via snapshot.
      for (auto *h : *hazards) {
        if (!h->is_alive()) continue;
        if (!shock_bolt_reaches(pts, h->position, h->radius)) continue;
        if (h->kind_of() == Hazard::SEEKER) {
          h->destroy();
          firer->score += Hazard::SEEKER_REWARD;
          if (station_explode_sound != NULL)
            Mix_PlayChannel(-1, station_explode_sound, 0);
        } else {
          h->hit();
          if (h->kind_of() == Hazard::COMET) {
            shed_comet_fragment(h);
            shed_comet_fragment(h);
          }
          if (!h->is_alive()) {
            firer->score += (h->kind_of() == Hazard::COMET)
                                ? Hazard::COMET_REWARD : Hazard::PULSAR_REWARD;
            if (station_explode_sound != NULL)
              Mix_PlayChannel(-1, station_explode_sound, 0);
          }
        }
      }
      // Friendly fire: the client's arc reached our (host) player. Credited
      // exactly like the host's own bolt-vs-partner (score, no achievement).
      if (friendly_fire && !players->empty()) {
        Ship *partner = players->front()->ship;
        if (partner->is_alive() && partner != firer &&
            shock_bolt_reaches(pts, partner->position, partner->radius))
          firer->shock_hit_ship(partner);
      }
      relay_others();  // PB-D4
      continue;
    }
    if (h.msg_type == Net::MSG_HIT_SHIP) {
      // Client bullet-vs-ship claim (PROTO 15). Exactly-once rule: the
      // damage applies IFF the referenced clone is consumed HERE — if
      // our own sim already resolved that bullet (hit something, TTL'd),
      // the claim is a no-op, so no shot ever counts twice.
      uint8_t kind = r.u8();
      uint32_t bullet_id = r.u32();
      uint32_t target_id = r.u32();  // PROTO 16: exact enemy claimed
      float hx = r.f32(), hy = r.f32();
      // PROTO 25: the claimant is the header's seat (deliberately shadows
      // the function-scope `remote` for the rest of this handler).
      Ship *remote = firer_by_header(h);
      if (!r.ok || !remote || !std::isfinite(hx) || !std::isfinite(hy))
        continue;
      if (bullet_id == 0) {
        // PROTO 20 lance sentinel (the twin of MSG_HIT's): no clone to
        // consume. Enemy claims only — kills are idempotent by wire id,
        // hull damage is not (it applies via the MSG_LANCE polyline).
        if (kind != 0) continue;
      } else {
        int bi = -1;
        for (size_t i = 0; i < remote->bullets.size(); i++)
          if (remote->bullets[i].net_id == bullet_id) { bi = (int)i; break; }
        if (bi < 0) {
          NET_LOG("net: ship hit claim no-op (bullet %u already resolved)\n",
                  bullet_id);
          continue;
        }
        Point bpos = remote->bullets[bi].position;
        remote->explode(bpos, Point(0.0f, 0.0f));
        remote->bullets[bi] = std::move(remote->bullets.back());
        remote->bullets.pop_back();
      }
      WrappedPoint hit_pos(hx, hy);
      if (kind == 0) {
        // PROTO 16: the claim names the exact enemy by wire id. Absent
        // or already dead = the host sim (or another claim) beat this
        // one to it — silent no-op, the kill still counted once.
        GLShip *target = NULL;
        for (auto *ge : *enemies)
          if (ge->ship->net_ship_id == target_id && ge->ship->is_alive()) {
            target = ge;
            break;
          }
        if (target != NULL && target->ship->kill_stop()) {
          target->ship->detonate();
          remote->kills_this_life += 1;
          remote->kills += 1;
          remote->score += target->ship->get_value() * remote->multiplier();
          // The claimant already played its own boom at consume; drop
          // this ship from the boom outbox so EV_WORLD_BOOM doesn't
          // double the cue (the host played its local sound in kill()).
          for (auto bi = Ship::net_booms.begin();
               bi != Ship::net_booms.end(); ++bi)
            if (*bi == target->ship) { Ship::net_booms.erase(bi); break; }
          NET_LOG("net: ship hit claim honored (enemy %u)\n", target_id);
        } else {
          // Already dead — usually our own resolution of the same lance
          // polyline (MSG_LANCE precedes the claims on the ordered
          // channel). The claimant played its own boom at its instant
          // kill, so drop any EV_WORLD_BOOM still queued for this ship
          // or the client hears the death twice.
          for (auto *ge : *enemies)
            if (ge->ship->net_ship_id == target_id) {
              for (auto bo = Ship::net_booms.begin();
                   bo != Ship::net_booms.end(); ++bo)
                if (*bo == ge->ship) { Ship::net_booms.erase(bo); break; }
              break;
            }
          NET_LOG("net: ship hit claim no-op (enemy %u already dead)\n",
                  target_id);
        }
      } else if (kind == 1 && station != NULL && station->is_alive()) {
        // Host-local hull-thud only — the claiming client already played
        // its own cue at consume (relaying EV_ROID_THUD would double it).
        WorldSound::play(Asteroid::thud_sound, station->position);
        station->hit();
        NET_LOG("net: ship hit claim honored (station)\n");
      } else if (kind == 2 && mini_station != NULL &&
                 mini_station->is_alive()) {
        remote->score += GLMiniStation::REWARD;
        mini_station->destroy();
        // The tick's destruction-sound block only fires for a mini that
        // was alive when it started — this one died between ticks, so
        // play + relay the boom here (same as the tick block does).
        play_priority_chunk(station_explode_sound,
                            net_listener_volume(mini_station->position));
        net_send_event(Net::EV_STATION_BOOM,
                       Net::pack_pos(mini_station->position.x(),
                                     mini_station->position.y(),
                                     world.x(), world.y()));
        NET_LOG("net: ship hit claim honored (mini-station)\n");
      }
      continue;
    }
    if (h.msg_type == Net::MSG_HIT) {
      // Client hit claim (PROTO 13): its screen saw its own bullet kill
      // this asteroid — honor it. The client only claims hits its local
      // rules say a plain bullet kills, so divergent host-side state
      // (tough health, teleport window, phase) is forced through rather
      // than re-litigated; invincible stays the one hard no (a claim for
      // one can only be a stale-flag artifact). The kill runs the normal
      // host path from here: this tick's reap spawns fragments and
      // drops, and the removal record replicates back — where the claim
      // sender already killed its copy, making that a no-op.
      uint32_t id = r.u32();
      uint32_t bullet_id = r.u32();  // PROTO 14: the killing bullet
      // PROTO 25: the claimant is the header's seat (shadows the outer
      // `remote` for this handler, like the MSG_HIT_SHIP case).
      Ship *remote = firer_by_header(h);
      if (!r.ok || !remote) continue;
      for (auto oi = objects->begin(); oi != objects->end(); ++oi) {
        Asteroid *a = *oi;
        if (a->net_id != id) continue;
        if (a->invincible || !a->is_alive()) break;
        if (a->tough) a->health = 1;
        if (a->teleporting) a->teleport_vulnerable = true;
        a->phased = false;
        if (a->kill()) {
          remote->score += a->get_value() * remote->multiplier();
          remote->kills_this_life += 1;
          remote->kills += 1;
          remote->tally_nova_kill(a->position);
          // The clone of the killing bullet is still in flight here (the
          // claim beat it by ~RTT/2) — left alone it plows into the
          // freshly-spawned fragments and takes one of them too. Spend
          // it by id (PROTO 14 exact match; ordered channel guarantees
          // the shot arrived first), nearest-at-impact as the fallback
          // for a clone the host sim already spent elsewhere.
          // bullet_id 0 (PROTO 18): a lance kill or a PIERCING bolt's
          // kill — there is nothing to spend (the lance has no clone;
          // a piercing clone must keep flying through the kill), and
          // the nearest-bullet fallback would wrongly eat an unrelated
          // shot. Skip the consume entirely.
          int best_i = -1;
          if (bullet_id != 0) {
            for (size_t bi = 0; bi < remote->bullets.size(); bi++)
              if (remote->bullets[bi].net_id == bullet_id) { best_i = (int)bi; break; }
            if (best_i < 0) {
              float best = a->radius + 200.0f;
              for (size_t bi = 0; bi < remote->bullets.size(); bi++) {
                float d = remote->bullets[bi].position.distance_to(a->position);
                if (d < best) { best = d; best_i = (int)bi; }
              }
            }
            if (best_i >= 0) {
              remote->explode(remote->bullets[best_i].position, a->velocity);
              remote->bullets[best_i] = std::move(remote->bullets.back());
              remote->bullets.pop_back();
            }
          }
          NET_LOG("net: hit claim honored id=%u (bullet %s)\n", id,
                  bullet_id == 0 ? "none (pierce/lance)"
                  : best_i >= 0  ? "consumed"
                                 : "already spent");
        }
        break;
      }
      continue;
    }
    if (h.msg_type != Net::MSG_INPUT) continue;

    Net::InputState in;
    if (!Net::decode_input(r, in)) continue;
    // Unreliable channel: drop stale/reordered packets (signed distance
    // handles seq wrap).
    if (peer.have_input && (int32_t)(in.seq - peer.last_input_seq) <= 0) {
      peer.input_stale_drops++;
      if (peer.gap_deadline) peer.gap_stragglers++;
      continue;
    }
    uint32_t seq_leap = peer.have_input ? in.seq - peer.last_input_seq : 1;
    peer.last_input_seq = in.seq;
    if (peer.gap_deadline) {
      peer.gap_accepts++;
      if (seq_leap > peer.gap_max_leap) peer.gap_max_leap = seq_leap;
    }
    // An INPUT blackout (unreliable channel dying under relay congestion)
    // is what big local-ship corrections on the CLIENT look like from
    // here — log the gap so the two logs can be correlated, then watch
    // the next 1.5 s to say what the gap actually WAS (see glgame.h).
    // The client mints a seq every 8 ms step, so "skipped" = it kept
    // sending into the void; zero skipped = it stopped stepping (or an
    // ordered backlog is about to replay — the window tells them apart).
    if (peer.have_input && current_time - peer.last_input_time > 300) {
      NET_LOG("net: input gap %d ms ended (seq %u, %u seqs skipped, "
              "%d stale rx during gap)\n",
              current_time - peer.last_input_time, in.seq, seq_leap - 1,
              peer.input_stale_drops);
      peer.gap_deadline = current_time + 1500;
      peer.gap_skipped = seq_leap - 1;
      peer.gap_stragglers = 0;
      peer.gap_accepts = 0;
      peer.gap_max_leap = 0;
    }
    peer.last_input_time = current_time;
    peer.input_stale_drops = 0;
    peer.input_zeroed = false;
    if (!remote) continue;

    if (!peer.have_input) {
      // First INPUT: baseline the one-shot counters instead of firing
      // whatever the client accumulated before we were listening.
      peer.have_input = true;
      peer.prev_boost = in.boost_count;
      peer.prev_next_weapon = in.next_weapon_count;
      peer.prev_next_secondary = in.next_secondary_count;
      peer.prev_teleport = in.teleport_count;
      peer.prev_respawn = in.respawn_count;
      peer.prev_shoot_press = in.shoot_press_count;
      peer.prev_secondary_press = in.secondary_press_count;
      // The first INPUT proves the client's game is up: if we are
      // paused (auto-paused on its disconnect, or by hand), share the
      // pause state now — an event sent while it was still in the lobby
      // would have been dropped. The friendly-fire room rule has the
      // same delivery problem (the ctor/rejoin announcements land while
      // the client is still bootstrapping and vanish — its HUD showed
      // OFF with the rule enabled), so re-announce it here too.
      // Targeted (B4): only the peer whose first INPUT just landed needs
      // the resync — re-syncing the others would blip their HUDs.
      if (!running) net_send_event_to(peer, Net::EV_PAUSE);
      net_send_event_to(peer, Net::EV_FRIENDLY_FIRE,
                        friendly_fire ? 1u : 0u);
      // Seat identities are the exception to the targeted rule: the
      // newcomer needs the whole roster AND the veterans need the
      // newcomer, and the store is idempotent — nothing blips.
      net_broadcast_seat_identities();
    }

    // One-shot deltas; capped so a rejoining/wrapped counter can't burst.
    uint8_t boosts = (uint8_t)(in.boost_count - peer.prev_boost);
    uint8_t weapons = (uint8_t)(in.next_weapon_count - peer.prev_next_weapon);
    uint8_t secondaries =
        (uint8_t)(in.next_secondary_count - peer.prev_next_secondary);
    uint8_t teleports = (uint8_t)(in.teleport_count - peer.prev_teleport);
    uint8_t respawns = (uint8_t)(in.respawn_count - peer.prev_respawn);
    uint8_t shot_presses = (uint8_t)(in.shoot_press_count - peer.prev_shoot_press);
    uint8_t sec_presses =
        (uint8_t)(in.secondary_press_count - peer.prev_secondary_press);
    peer.prev_shoot_press = in.shoot_press_count;
    peer.prev_secondary_press = in.secondary_press_count;
    peer.prev_boost = in.boost_count;
    peer.prev_next_weapon = in.next_weapon_count;
    peer.prev_next_secondary = in.next_secondary_count;
    peer.prev_teleport = in.teleport_count;
    peer.prev_respawn = in.respawn_count;

    if (!remote->is_alive()) {
      // Dead ships take no control input, same as the local player. Keys
      // still held at death stay suppressed through the respawn until the
      // player releases and re-presses them — respawn's reset() gives the
      // local player exactly that restriction.
      peer.held_suppress = 0xffff;
      // Mirror the local respawn-tap rule (GLShip::input): a shoot tap
      // while dead, after a short grace period, skips the countdown.
      if (respawns && remote->lives > 0 &&
          remote->time_until_respawn <= remote->respawn_time - 1000)
        remote->time_until_respawn = 0;
      continue;
    }

    // A suppressed bit (set at each level transition) stays ignored while
    // continuously held and expires the first time the client reports it
    // released — the remote player re-presses each key exactly like the
    // local player does after respawn's reset().
    peer.held_suppress &= in.held;
    uint16_t held = in.held & (uint16_t)~peer.held_suppress;

    remote->rotation_scale = in.analog_rotation;
    remote->thrust_analog = in.analog_thrust;
    remote->reverse_analog = in.analog_reverse;
    // Aim is client-authoritative: adopt the exact facing the client's
    // screen shows (rotation inertia, analog rate and sensitivity all
    // included) — the client's local aim is never snapshot-corrected, so
    // reconstructing rotation host-side from the on/off bits drifted and
    // bullets left the nose at a visibly wrong angle. The held rotate
    // bits below still extrapolate between INPUT arrivals.
    float fmag = sqrtf(in.facing_x * in.facing_x + in.facing_y * in.facing_y);
    if (fmag > 0.5f && fmag < 2.0f)
      remote->facing = Point(in.facing_x / fmag, in.facing_y / fmag);
    // Pose is client-authoritative too (v12): adopt it, so the pilot is
    // never rubberbanded by corrections and collisions computed here
    // match what that player actually saw. Gated on the warp echo —
    // after a host-driven respawn/teleport, in-flight INPUTs still carry
    // the pre-warp pose and would drag the ship straight back. Sanity
    // checks only, not anti-cheat (co-op): finite values, plausible speed.
    if (in.warp_echo == remote->net_warp_count &&
        std::isfinite(in.pos_x) && std::isfinite(in.pos_y) &&
        std::isfinite(in.vel_x) && std::isfinite(in.vel_y) &&
        in.vel_x * in.vel_x + in.vel_y * in.vel_y < 9.0f) {
      WrappedPoint reported(in.pos_x, in.pos_y);
      reported.wrap();
      Point c = reported.closest_to(remote->position);
      float dx = c.x() - remote->position.x();
      float dy = c.y() - remote->position.y();
      float err2 = dx * dx + dy * dy;
      remote->velocity = Point(in.vel_x, in.vel_y);
      if (err2 < 40.0f * 40.0f || err2 > 600.0f * 600.0f) {
        // Steady state (125 Hz reports differ by a step or two) or a
        // teleport-scale jump: take the pose whole.
        remote->position = reported;
        remote->net_pose_err = Point(0.0f, 0.0f);
      } else {
        // Post-blackout catch-up: the client's report leaps to wherever
        // it really flew during the gap. Bank the leap and let the host
        // tick's drain glide the visible ship there over ~150 ms — the
        // hop the HOST player was seeing. Gameplay pose is momentarily
        // stale, which is no worse than the blackout itself was.
        remote->net_pose_err = Point(dx, dy);
      }
    }
    remote->rotate_left((held & Net::IN_LEFT) != 0);
    remote->rotate_right((held & Net::IN_RIGHT) != 0);
    remote->thrust((held & Net::IN_THRUST) != 0);
    remote->reverse((held & Net::IN_REVERSE) != 0);
    // Trigger rule: a new press arms the weapon; releasing disarms it; a
    // held key with no new press leaves the weapon alone. Semi-automatics
    // (which disarm themselves after each shot) thus fire once per press,
    // automatics keep firing while held, and hold-style weapons like the
    // shield stay active — all without inspecting the weapon type.
    // Presses are QUEUED (clamped like the other wrap counters below), not
    // collapsed into one shoot(true): a lost-INPUT blackout straddling two
    // real semi-auto fires delivers shot_presses==2 on recovery, and firing
    // once for two client fires desynced the ammo — Ship::step replays one
    // queued press per step so the host decrements once per CLIENT press.
    // Absolute backlog cap on both queues (the per-message clamp alone
    // let INPUTs arriving faster than the 125 Hz drain grow them without
    // bound): 32 queued presses is ~256 ms of drain, far beyond any human
    // burst, and past it the oldest presses are simply already served.
    // Sanity, not anti-cheat (co-op) — it just keeps the int bounded.
    const int PRESS_BACKLOG_MAX = 32;
    if (shot_presses) {
      remote->net_queued_shot_presses +=
          (shot_presses > 4 ? 4 : shot_presses);
      if (remote->net_queued_shot_presses > PRESS_BACKLOG_MAX)
        remote->net_queued_shot_presses = PRESS_BACKLOG_MAX;
    } else if (!(held & Net::IN_SHOOT) &&
             remote->net_queued_shot_presses == 0)
      remote->shoot(false);
    // Secondaries queue on exactly the same terms (see the drain in
    // Ship::step): one deploy per press, none of them automatic, so a
    // collapsed batch made fewer mines/missiles than the client fired.
    if (sec_presses) {
      remote->net_queued_secondary_presses +=
          (sec_presses > 4 ? 4 : sec_presses);
      if (remote->net_queued_secondary_presses > PRESS_BACKLOG_MAX)
        remote->net_queued_secondary_presses = PRESS_BACKLOG_MAX;
    } else if (!(held & Net::IN_SECONDARY) &&
             remote->net_queued_secondary_presses == 0)
      remote->fire_secondary(false);

    if (boosts > 4) boosts = 4;
    if (weapons > 4) weapons = 4;
    if (secondaries > 4) secondaries = 4;
    if (teleports > 4) teleports = 4;
    while (boosts--) remote->boost();
    while (weapons--) remote->next_weapon();
    while (secondaries--) remote->next_secondary_weapon();
    while (teleports--) remote->add_behaviour(new Teleport(remote));
  }

  // Dead-man switch: no INPUT for 1 s (loss burst, hung tab) — release the
  // remote ship's held actions instead of letting it fly into a wall.
  if (remote && peer.have_input && !peer.input_zeroed &&
      current_time - peer.last_input_time > 1000) {
    peer.input_zeroed = true;
    remote->rotate_left(false);
    remote->rotate_right(false);
    remote->thrust(false);
    remote->reverse(false);
    remote->shoot(false);
    remote->fire_secondary(false);
  }
}

void GLGame::net_host_poll() {
  for (NetPeer *p : net_peers_)
    // A LOST peer's session is a door adoption mid-handshake (B5): its
    // transport belongs to NetSession::update until Ready — draining it
    // here would eat the HELLO.
    if (p->session && !p->lost) net_host_poll_peer(*p);
}

void GLGame::net_adopt_signal(NetSignal *signal, const std::string &room_code,
                              const std::vector<std::string> &ice_servers,
                              const std::string &room_token) {
  net_signal_ = signal;
  net_room_code_ = room_code;
  net_room_token_ = room_token;
  net_ice_ = ice_servers;
  // First process-death resume checkpoint the moment the room identity
  // exists — the session is resumable from minute one, not first pause.
  net_host_resume_persist();
}

void GLGame::net_send_local_identity() {
  if (!net_signal_) return;
  const NetIdentity &me = net_local_identity();
  net_signal_->send_identity(me.platform, me.name,
                             net_local_verify_credential());
}

// Recompose the initial JOINED / "JOINED X SERVER" greeting. The net
// constructors compose it before the lobby hands over the worker attestation
// and the worker-session context, so the first composition can never show a
// name (strict default context + claim-only identity); the lobby's
// post-construction net_set_worker_session / net_apply_peer_attestation
// calls land here to redo it with the final state — and so does a worker
// attestation that arrives IN-GAME (fast ICE hands off before the ~300 ms
// verify round-trip completes; Event::Identity below). That late caller is
// why the join-window guard exists: recomposing is only legal while the
// greeting is still the banner on screen — net_join_banner_text_ remembers
// what this function last composed, and any other banner (LEVEL,
// RECONNECTED, ...) or an expired timer closes the window.
void GLGame::net_drop_session() {
  NetPeer *p = net_peer();
  if (p) net_drop_session(*p);
}

void GLGame::net_drop_session(NetPeer &p) {
  delete p.session;  // closes + deletes the transport
  p.session = nullptr;
}

void GLGame::net_refresh_join_banner() {
  if (!net_join_banner_text_.empty() &&
      (net_banner_ms_ <= 0 || net_banner_text_ != net_join_banner_text_))
    return;  // the greeting is no longer showing — nothing to refresh
  std::string prev = net_banner_text_;
  if (net_mode_ == NetHost)
    net_banner_text_ =
        net_identity_name_or(net_peer_make().identity, net_peer_fallback().c_str(),
                             net_id_ctx()) +
        " JOINED";
  else if (net_mode_ == NetClient)
    net_banner_text_ = "JOINED " +
        net_identity_name_or(net_peer_make().identity, net_peer_fallback().c_str(),
                             net_id_ctx()) +
        " SERVER";
  else
    return;
  net_join_banner_text_ = net_banner_text_;
  // Log on change only: the ctor composition logs once, and a recompose
  // that actually renames the greeting logs the final text (greppable —
  // the e2e assertions match these lines).
  if (net_banner_text_ != prev)
    NET_LOG("net: banner '%s' %d ms\n", net_banner_text_.c_str(),
            net_banner_ms_);
}

// M3-1 reclaim countdown, shared by both host signal loops: the relay
// socket dropped, so count down and reattach to the room with the token.
void GLGame::net_host_signal_reclaim_tick(int delta) {
  if (net_signal_retry_ms_ <= 0) return;
  net_signal_retry_ms_ -= delta;
  if (net_signal_retry_ms_ > 0) return;
  net_signal_retry_ms_ = 0;
  net_ice_.clear();  // fresh TURN creds ride the reclaim reply
  net_signal_->connect_host_reclaim(net_signal_url(), net_room_code_,
                                    net_room_token_);
  NET_LOG("net: reclaiming room %s\n", net_room_code_.c_str());
}

// Signal events both host loops must treat identically: TURN creds,
// socket drop (schedule a reclaim, or give up on the pre-token relay),
// and relay errors (back off when rate-limited, drop the room when it
// is gone for good).
GLGame::NetSignalEventResult
GLGame::net_host_signal_common_event(const NetSignal::Event &ev) {
  switch (ev.kind) {
    case NetSignal::Event::Ice:
      net_ice_.push_back(ev.text);
      if (net_rehost_) net_rehost_->set_ice_servers(net_ice_);
      return NetSigHandled;
    case NetSignal::Event::Identity:
      // Worker peer attestation (NETPLAY.md V0): a rejoiner re-attests, so
      // refresh the badge in-game. Only a verified result promotes fields,
      // and only for the PAIRED joiner: the multi-join worker (B3) stamps
      // the announcing jid, and an unmatched stamp is another joiner in
      // the room — folding it here would rename our live peer. Both empty
      // = the pre-multi-join worker, which only ever relayed the paired
      // joiner's identity.
      if (ev.verified) {
        NetIdentity att;
        att.platform = ev.platform;
        att.platform_trust = NET_TRUST_ATTESTED;
        att.name = net_sanitize_name(ev.text);
        att.name_trust =
            att.name.empty() ? NET_TRUST_ABSENT : NET_TRUST_ATTESTED;
        // Bank it by jid FIRST (see net_jid_attested_): a verdict that
        // beats the door's answer belongs to a peer that does not own its
        // jid yet, and dropping it is what the admission gate would read
        // as an anonymous pilot. Bounded — these are worth seconds, so a
        // pathological pile of unanswered announces is simply discarded.
        if (!ev.peer.empty()) {
          if (net_jid_attested_.size() > 16) net_jid_attested_.clear();
          net_jid_attested_[ev.peer] = att;
          // ...and onto a pending join waiting on this very socket, so the
          // answer survives the cache above clearing (O4).
          std::map<std::string, PendingJoin>::iterator pj =
              net_pending_joins_.find(ev.peer);
          if (pj != net_pending_joins_.end()) pj->second.ident = att;
        }
        // B5: the stamp picks WHICH peer's badge this is — match it over
        // the roster's jids (each set at its adoption). An empty stamp is
        // the pre-multi-join worker, which only ever relays the single
        // paired joiner's identity: the front peer.
        NetPeer *tp = nullptr;
        if (ev.peer.empty()) {
          tp = net_peer();
        } else {
          for (NetPeer *p : net_peers_)
            if (p->jid == ev.peer) { tp = p; break; }
        }
        if (tp) {
          // Keep the raw attestation too: the rejoin-Ready refresh replaces
          // the peer's identity with the claim-only wire parse and re-folds
          // this copy so the badge survives the handshake.
          tp->attested = att;
          if (tp->lost) {
            // Mid-rejoin (the common ordering: worker verify ~300 ms beats
            // p2p Ready by seconds), and the jid-matched peer is the DOOR
            // seat — which the seat resolver may yet re-map. Folding onto
            // its identity here would overwrite a PARKED seat's remembered
            // pilot with the rejoiner's name, making the resolver see two
            // seats remembering the same pilot (ambiguous → door pick):
            // the exact hull swap rejoin-by-identity exists to prevent,
            // in every attested-platform room. Park-time memory must stay
            // untouched until the WELCOME settles who this is — the Ready
            // handler re-folds `attested` onto the fresh wire identity of
            // whichever seat the adoption actually lands on.
            NET_LOG("net: identity attested mid-rejoin (name='%s') - "
                    "deferred to adoption\n", att.name.c_str());
          } else {
            net_apply_attested(tp->identity, att);
            // Fast-ICE ordering: the hand-off can beat the verify
            // round-trip, in which case the JOINED greeting composed with
            // the role label is still on screen — rename it now that the
            // attested name is known (the join-window guard makes this a
            // no-op once any other banner has taken over).
            net_refresh_join_banner();
            NET_LOG("net: identity attested name='%s' platform=%s(%u)\n",
                    att.name.c_str(), net_platform_label(ev.platform),
                    (unsigned)ev.platform);
            // The other clients' HUD rows show this name too (4P) —
            // re-relay the roster now that a seat's badge changed.
            net_broadcast_seat_identities();
          }
        }
      } else if (!ev.peer.empty()) {
        // Unverified claim. It promotes nothing and renders nowhere — the
        // display rules are unchanged, an online session still shows only
        // attested fields — but it IS an answer to "who is on that
        // socket", which is all the flap resolver needs (O4, and see
        // net_jid_claimed_ for why a claim is the right currency there).
        NetIdentity cl;
        cl.platform = ev.platform;
        cl.platform_trust = NET_TRUST_CLAIMED;
        cl.name = net_sanitize_name(ev.text);
        cl.name_trust = cl.name.empty() ? NET_TRUST_ABSENT : NET_TRUST_CLAIMED;
        if (net_jid_claimed_.size() > 16) net_jid_claimed_.clear();
        net_jid_claimed_[ev.peer] = cl;
        std::map<std::string, PendingJoin>::iterator pj =
            net_pending_joins_.find(ev.peer);
        // A verified upgrade lands later and overwrites this, which is the
        // order we want: the claim answers "who" early, the attestation
        // answers it better.
        if (pj != net_pending_joins_.end() && pj->second.ident.name.empty())
          pj->second.ident = cl;
      }
      return NetSigHandled;
    case NetSignal::Event::Closed:
      // Warm a fresh verification credential the moment the reclaim
      // countdown arms: the mint is async (seconds on Steam/Play Games) and
      // the reclaim's identity re-announce reads the cache — warming here
      // means the ticket exists by the time the Room event re-attests,
      // instead of the announce shipping credential-less. No-op off
      // verify-backend builds.
      if (net_signal_retry_ms_ <= 0) (void)net_local_verify_credential();
      if (net_room_token_.empty()) {
        // Pre-token relay: no reclaim protocol — drop the signal (the
        // old behaviour; rejoin stops being possible).
        net_signal_->close();
        delete net_signal_;
        net_signal_ = nullptr;
        return NetSigDropped;
      }
      if (net_signal_retry_ms_ <= 0) net_signal_retry_ms_ = 3000;
      return NetSigHandled;
    case NetSignal::Event::Error:
      // rate-limited: the room is still ours (2h TTL, valid token) — the
      // per-IP host limiter just throttled this reclaim. Back off and
      // retry rather than abandoning a live room, same as the joiner.
      if (ev.text == "rate-limited") {
        net_signal_retry_ms_ = 15000;
        return NetSigHandled;
      }
      // room-in-use: after an ABRUPT drop (wifi/sleep) our OWN previous
      // socket is often still registered on the relay — the reclaim finds
      // hostWs() truthy before the dead socket's close is detected. The room
      // is still ours (valid token); retry until the ghost is evicted (the
      // relay now does this on the first valid-token reclaim, but an older
      // relay just needs it reaped). Dropping here was Glenn's wifi-off
      // "CONNECTION LOST".
      if (ev.text == "room-in-use") {
        net_signal_retry_ms_ = 3000;
        return NetSigHandled;
      }
      // no-such-room / expired on reclaim: the room is gone for good —
      // and with it any process-death resume of this session.
      NET_LOG("net: room %s lost (%s)\n", net_room_code_.c_str(),
             ev.text.c_str());
      NetResume::clear_with_save();
      net_room_token_.clear();  // stop the ticket refresh re-minting it
      net_signal_->close();
      delete net_signal_;
      net_signal_ = nullptr;
      return NetSigDropped;
    default:
      return NetSigUnhandled;
  }
}

// M3-1 mobile lifecycle: the relay socket can drop while the game is fine
// (host app backgrounded, wifi blip). Reclaim the room with the token so
// later rejoins stay possible, and treat a peer-join notification as
// instant client-loss detection — the client would not be rejoining the
// room if its transport were alive (ICE takes ~10 s to say so).
void GLGame::net_host_signal_maintain(int delta) {
  // Reached only while no peer is lost, which makes it the one place that
  // is unambiguously BETWEEN loss episodes — so it is where pending joins
  // die. The checker's own rule handles the cases it can see, but it only
  // runs while a seat is lost, and the last record of an episode can slip
  // past it: a healthy adoption keeps hitting the connected() early
  // return, then reaches Ready and the whole block stops running with the
  // record still filed. Left there, a later episode's slow-but-honest
  // handshake by the same pilot could match it. Clearing at this boundary
  // (and NOT at park, which fires per seat and would discard live
  // evidence mid-episode) is the version of the rule that holds.
  net_pending_joins_.clear();
  net_host_signal_reclaim_tick(delta);

  NetSignal::Event ev;
  while (net_signal_->poll(ev)) {
    NetSignalEventResult common = net_host_signal_common_event(ev);
    if (common == NetSigDropped) return;
    if (common == NetSigHandled) continue;
    switch (ev.kind) {
      case NetSignal::Event::Room:
        NET_LOG("net: room %s reclaimed\n", net_room_code_.c_str());
        net_send_local_identity();  // re-attest for the (re)joiner
        break;
      case NetSignal::Event::PeerJoin:
        // The client re-entered the room: its transport is dead even if
        // ours has not noticed yet. Enter the rejoin flow immediately.
        // B5: only unambiguous with ONE peer — a rejoiner arrives on a
        // FRESH socket (fresh jid), so at N>1 a join can't be mapped to
        // a seat; the real loss surfaces via its own transport watch /
        // RX watchdog within seconds instead.
        if (net_peers_.size() != 1) {
          NET_LOG("net: peer joined the room (N>1, awaiting loss detect)\n");
          break;
        }
        NET_LOG("net: peer rejoined the room - fast loss detect\n");
        net_drop_session();
        net_peer_make().lost = true;
        return;  // net_host_rejoin_poll owns the signal from here
      default:
        break;
    }
  }
}

// First tick after a SEAT's loss (shared by the relay and LAN rejoin
// doors, whichever notices first): park that seat's ship. An alive ship
// stays visible where it stood, motionless with the shield up
// (invincible), until its owner rejoins; a dead one keeps its corpse
// frozen (no respawn countdown bleeding lives into drifting asteroids).
// Held inputs are cleared in case the dead-man switch hadn't zeroed
// them yet. Guarded to run ONCE per loss per seat (p.parked).
// Pause policy (PB-D7): pausing is the sensible default while a door is
// open for a rejoin — but only when the departed peer was the LAST live
// one; three players don't wait for one, so with another remote peer
// still in the room play continues and the parked hull sits shielded.
// The room-level pause latch (net_rejoin_parked_) guards toggle_pause
// (it toggles — a second call would silently unpause) and resets when
// any rejoin completes.
// Host: remove a peer from the room (FOURPLAYER.md O3). Tell them first —
// a peer that only saw its transport die would come straight back through
// the rejoin door and undo this — then take the ordinary loss path, which
// already knows how to free a seat: park frees the hull and opens the
// door, so the seat is immediately available to whoever joins next.
//
// KICK and BAN are two actions, not one (the host picks with left/right on
// the row). A kick removes a wedged or unwanted peer and lets them come
// back — the ordinary case in a co-op game played with friends, where the
// fix for someone stuck at the wrong seat should not also be a punishment.
// A ban additionally refuses their next handshake. Everything else about
// the two is identical, which is why this is one function with a flag.
void GLGame::net_kick_peer(NetPeer &p, bool ban) {
  if (net_mode_ != NetHost) return;
  NET_LOG("net: %s player %d\n", ban ? "banning" : "kicking", (int)p.seat);
  if (p.session) {
    net_send_event_to(p, Net::EV_KICKED);
    // The session is NOT dropped here — see net_kick_closing_. The peer
    // is parked below (seat freed, hull frozen, sim ignores it), and the
    // transport is torn down a few ticks later once the goodbye has had
    // time to leave. Appended, not assigned: a second kick inside this
    // window must not steal the first victim's drain slot.
    net_kick_closing_.push_back(std::make_pair(p.seat, 600));
  }
  // Ban only: bar them from coming back. The event above stops a
  // well-behaved client either way, but the room code is in their hands and
  // nothing else would refuse a fresh join. Identity-keyed (see
  // NetLobby::ban_identity): a nameless peer can't be banned, only kicked.
  //
  // BOTH the folded identity and the raw HELLO claim: p.identity carries
  // the worker's attestation folded over it, while enforcement can only
  // ever see the claim (that is all a fresh handshake has at the moment
  // it must decide). Banning only the attested form silently fails to
  // match wherever the two differ, and the peer walks back in.
  if (ban) {
    if (p.session) NetLobby::ban_identity(p.session->peer_identity());
    NetLobby::ban_identity(p.identity);
    if (!p.attested.name.empty()) NetLobby::ban_identity(p.attested);
  }
  // Park frees the hull and opens the rejoin door (the seat becomes
  // available). The session dies on the timer above — parking alone
  // would leave a kicked peer connected and frozen.
  p.lost = true;
  // keep_session: park's own net_drop_session would destroy the channel
  // with the goodbye still queued in it — the exact thing the timer above
  // exists to avoid.
  net_host_rejoin_park_peer(p, /*keep_session=*/true);
}

void GLGame::net_host_rejoin_park_peer(NetPeer &p, bool keep_session) {
  if (p.parked) return;
  p.parked = true;
  // The departed peer's worker attestation dies with them: whoever fills
  // the slot re-attests through their own announce (Event::Identity), and
  // without this a DIFFERENT friend whose verify failed would inherit the
  // old friend's attested name. p.identity itself survives — the
  // DISCONNECTED banner still names who dropped.
  p.attested = NetIdentity();
  p.adopt_claim = NetIdentity();  // same rule for the unattested twin
  // Deliberately does NOT clear net_pending_joins_. Park runs per newly
  // lost SEAT, not once per episode, so a second seat dropping while a
  // returning pilot's join is still being identified would wipe that
  // record — and a PeerJoin fires once per socket, so nothing could file
  // it again and the corpse would live out its full ICE timeout. The
  // records' lifetime is enforced where it belongs, in
  // net_host_rejoin_flap_check: no relay adoption in flight, no records.
  p.flap_dropped = false;
  GLShip *gs = player_by_seat(p.seat);
  Ship *remote = gs ? gs->ship : NULL;
  if (remote) {
    if (remote->is_alive()) {
      remote->velocity = Point(0, 0);
      remote->invincible = true;
      remote->time_left_invincible = 1 << 29;
      remote->rotate_left(false);
      remote->rotate_right(false);
      remote->thrust(false);
      remote->reverse(false);
      remote->shoot(false);
      remote->fire_secondary(false);
    } else {
      remote->time_until_respawn = 1 << 29;
    }
  }
  if (!keep_session) net_drop_session(p);
  if (net_all_peers_lost() && !net_rejoin_parked_) {
    net_rejoin_parked_ = true;
    if (running) {
      toggle_pause(false);
      NET_LOG("net: paused awaiting rejoin\n");
    }
  }
  net_host_resume_persist();  // client-leave checkpoint (see NETPLAY.md)
}

// A seat gone but the room is still open: keep simulating, offer a
// fresh transport through the room, and resume when the peer rejoins
// (a rejoin is a plain JOIN on their side — full snapshot bootstrap).
// B5: the door serves the LOWEST parked seat first (net_door_peer); a
// second dropped seat waits its turn — the worker admits its rejoiner,
// whose answer waits for our next offer once this adoption completes.
void GLGame::net_host_rejoin_poll(int delta) {

  // Arm once per open seat: only while there is no live rehost offer AND
  // no door adoption mid-handshake (a fresh session created from a
  // rejoin answer is Handshaking on a HEALTHY transport — leave it be).
  NetPeer *door = net_door_peer();
  if (!net_rehost_ && !net_handshaking_lost_peer() && door) {
    net_rehost_seat_ = door->seat;
    net_rehost_ = NetTransport::create();
    net_rehost_offer_sent_ = false;
    net_rehost_cands_.clear();  // fresh transport, fresh trickle stream
    if (net_rehost_) {
      net_rehost_->set_ice_servers(net_ice_);
      net_rehost_->set_trickle(true);  // the room relay carries candidates
      net_rehost_->start_host();
    }
    // "player N lost/rejoined" is the greppable rejoin contract
    // (TESTING.md; rejoin/hiccup/turnexpiry/lankeep e2e) — at seat 2 the
    // text is byte-identical to the 2P era's.
    NET_LOG("net: player %d lost - room %s reopened for rejoin\n",
           (int)door->seat, net_room_code_.c_str());
  }

  // Signal socket dropped mid-rejoin: reclaim the room (M3-1) with the
  // same countdown the healthy path uses; the offer re-sends once the
  // reclaimed Room frame arrives.
  net_host_signal_reclaim_tick(delta);

  if (net_rehost_ && !net_rehost_offer_sent_ && net_signal_retry_ms_ <= 0 &&
      net_rehost_->local_description_ready()) {
    net_signal_->send_offer(net_rehost_->local_description());
    net_rehost_offer_sent_ = true;
    // A RE-push (watchdog, room reclaim): the worker wiped its stored
    // candidates with the offer and the transport's trickle stream was
    // drained on the first send — replay the cache so a TURN-only
    // rejoiner still connects on the first try (first send: cache empty,
    // no-op; ICE discards duplicates harmlessly).
    for (const std::string &c : net_rehost_cands_) {
      size_t nl = c.find('\n');
      if (nl != std::string::npos)
        net_signal_->send_cand(c.substr(0, nl), c.substr(nl + 1));
    }
  }

  // Trickle ICE (M3-2b): stream the rehost transport's candidates to the
  // rejoining client through the room — only once the offer is out (a
  // candidate arriving before it gets wiped with the relay's stale-cand
  // buffer when the offer lands). The mid-handshake fallback is the DOOR
  // peer's fresh session, never a healthy peer's (B5).
  NetPeer *hs = net_handshaking_lost_peer();
  if (net_signal_retry_ms_ <= 0 && (net_rehost_offer_sent_ || hs)) {
    NetTransport *t =
        net_rehost_ ? net_rehost_ : (hs ? hs->session->transport() : nullptr);
    std::string c;
    while (t && t->poll_local_candidate(c)) {
      // The rehost transport's candidates are kept for the re-push
      // replay above (poll_local_candidate drains each exactly once).
      if (t == net_rehost_) net_rehost_cands_.push_back(c);
      size_t nl = c.find('\n');
      if (nl != std::string::npos)
        net_signal_->send_cand(c.substr(0, nl), c.substr(nl + 1));
    }
  }

  NetSignal::Event ev;
  while (net_signal_->poll(ev)) {
    NetSignalEventResult common = net_host_signal_common_event(ev);
    if (common == NetSigDropped) return;
    if (common == NetSigHandled) continue;
    if (ev.kind == NetSignal::Event::Answer && net_rehost_) {
      // Version gate, as in the lobby: a rejoiner on a different build
      // can't bootstrap — ignore its answer and keep the offer open.
      if (ev.text2 != std::to_string((int)Net::PROTO_VERSION)) {
        NET_LOG("net: rejoin answer pv '%s' != ours %d - ignored\n",
               ev.text2.c_str(), (int)Net::PROTO_VERSION);
        continue;
      }
      NetPeer *dp = net_peer_by_seat(net_rehost_seat_);
      if (!dp || dp->session) {  // seat vanished or already adopting
        NET_LOG("net: rejoin answer for busy seat %d - ignored\n",
                net_rehost_seat_);
        continue;
      }
      net_rehost_->set_remote_answer(ev.text);
      dp->session =
          new NetSession(net_rehost_, NetSession::HostRole, dp->seat);
      dp->adopt_ms = 0;  // handshake clock (flap resolver)
      // Reset FIRST, fill second. A peer can host several adoptions in one
      // loss episode (a failed attempt drops the session but the seat stays
      // parked), and a conditional fill would leave the PREVIOUS pilot's
      // claim in place whenever the new socket has none banked — handing
      // the flap resolver a `mid` belonging to someone who is no longer
      // there, which is how it would tear down the healthy handshake of
      // whoever is.
      dp->adopt_claim = NetIdentity();
      // The attestation resets first too, and for a reason of its own: a
      // verified Identity for the DEAD jid can land after a drop (the
      // peer keeps its jid until this line, and the fold matches on jid
      // alone), so a conditional fill would leave the previous pilot's
      // attested name for the next one to wear at the Ready re-fold.
      dp->attested = NetIdentity();
      // Take this socket's CLAIM the same way the attestation below is
      // taken: onto the adoption, off the transient jid map. Without it
      // the resolver's "who is mid-handshake" answer lived only as long
      // as a bounded cache nobody prunes deliberately.
      {
        std::map<std::string, NetIdentity>::iterator cit =
            net_jid_claimed_.find(ev.peer);
        if (cit != net_jid_claimed_.end()) {
          dp->adopt_claim = cit->second;
          net_jid_claimed_.erase(cit);
        }
      }
      // Rejoin-by-identity: let the handshake re-map the WELCOME to the
      // parked seat whose remembered pilot the HELLO claim matches — the
      // Ready handler adopts onto the seat actually assigned.
      dp->session->set_seat_resolver([this](const NetIdentity &claimed) {
        return net_rejoin_seat_for_identity(claimed);
      });
      net_install_admit_check(dp->session, (int)dp->seat);
      // A relay rejoin re-pairs through the worker: whoever this adoption
      // LANDS on loses any offline display carve-out (attestation
      // governs). Recorded on the adoption, not the door peer — the seat
      // resolver may re-map the WELCOME, and mutating the door guess here
      // wrongly stripped a still-parked LAN seat's carve-out when the
      // adoption moved elsewhere. Applied at Ready.
      net_rehost_adopt_lan_ = false;
      net_rehost_ = nullptr;
      // The answering joiner is the peer this session binds to (B3 `from`
      // stamp; empty against an old worker) — the Identity fold matches
      // over the roster's jids, so a rejoiner's fresh jid must land on
      // its peer. net_peer_jid_ is the lobby-era mirror, kept in step
      // for the front peer only.
      dp->jid = ev.peer;
      if (dp == net_peer()) net_peer_jid_ = ev.peer;
      // The seat now owns this jid, so any verdict that arrived before it
      // did is deliverable (net_jid_attested_). Onto `attested` only —
      // NEVER onto dp->identity, which is the parked seat's remembered
      // pilot and what the seat resolver matches the HELLO claim against.
      // Drained on consume; a later re-attestation reaches the peer by jid
      // through the ordinary fold.
      {
        std::map<std::string, NetIdentity>::iterator ait =
            net_jid_attested_.find(ev.peer);
        if (ait != net_jid_attested_.end()) {
          dp->attested = ait->second;
          NET_LOG("net: identity attested before the answer - applied to "
                  "seat %d (name='%s')\n",
                  (int)dp->seat, ait->second.name.c_str());
          net_jid_attested_.erase(ait);
        }
      }
      // B5, N>1: the LAN beacon still open is serving THIS seat's blob —
      // a different seat's rejoiner completing it now would be adopted
      // onto the wrong slot. The relay won this seat; close the LAN door
      // and let it re-arm fresh for the next parked seat (at one peer
      // the doors race for the same human, master behaviour — keep it).
      if (net_peers_.size() > 1) net_lan_rejoin_reset();
    } else if (ev.kind == NetSignal::Event::Cand) {
      // Re-resolved live (not the pre-loop `hs`): the Answer branch above
      // may have JUST created the door session in this same poll batch.
      // Stamped frames must match the adoption's jid — a SECOND
      // rejoiner's candidates belong to a future offer, not this
      // transport (ICE would discard them, but why feed it noise).
      NetPeer *hs2 = net_handshaking_lost_peer();
      if (hs2 && !ev.peer.empty() && hs2->jid != ev.peer) continue;
      NetTransport *t =
          net_rehost_ ? net_rehost_
                      : (hs2 ? hs2->session->transport() : nullptr);
      if (t) t->add_remote_candidate(ev.text2, ev.text);
    } else if (ev.kind == NetSignal::Event::Room) {
      // Reclaimed mid-rejoin: the room's stored offer died with the old
      // socket — resend the current one.
      NET_LOG("net: room %s reclaimed (mid-rejoin)\n", net_room_code_.c_str());
      net_rehost_offer_sent_ = false;
      net_send_local_identity();  // re-attest for the (re)joiner
    } else if (ev.kind == NetSignal::Event::PeerJoin && net_peers_.size() == 1 &&
               net_session()) {
      // A rejoiner re-entered the room while we ALREADY have a handshaking
      // session with them. The relay sends the host a PeerJoin the instant a
      // joiner arrives, BEFORE its answer — so the normal single rejoin's
      // PeerJoin lands while net_session() is still null (offer out, awaiting
      // the answer) and must be left alone. Tearing that offer down restarted
      // ICE on every join event and looped forever over a real TURN path
      // (Glenn: "AGES to reconnect", re-offering ~0.7 s after the answer while
      // candidates were still flowing). A PeerJoin AFTER a session exists is a
      // genuine SECOND join — their prior attempt's transport flapped — so the
      // half-open session is a corpse the joiner is no longer party to. Drop
      // it and let the top-of-poll block re-offer a fresh one next tick,
      // instead of waiting out its ~30 s ICE timeout.
      NET_LOG("net: peer re-joined onto a stale session - re-offering\n");
      // The abandoned attempt's attestation goes with its session (same
      // reasoning as the Failed/Rejected branch): the re-entering joiner
      // re-announces on its fresh socket and re-attests through the worker.
      net_peer_make().attested = NetIdentity();
      net_drop_session();
      net_rehost_offer_sent_ = false;
      return;  // top-of-poll re-offer owns the signal from here
    } else if (ev.kind == NetSignal::Event::PeerJoin && net_peers_.size() > 1) {
      // The N>1 twin of the branch above (O4). The inference that made
      // that one safe — "a join while a session exists is that session's
      // owner coming back" — does not hold here: the relay mints a fresh
      // jid per socket, so this join could be a different player arriving
      // for another parked seat, and tearing down an in-flight handshake
      // on that guess is the "AGES to reconnect" bug. So don't guess:
      // note the jid and let net_host_rejoin_flap_check ask who it is
      // once the worker says.
      NetPeer *hs = net_handshaking_lost_peer();
      if (hs && !ev.peer.empty() && ev.peer != hs->jid) {
        // Full: evict the oldest ARRIVAL, not the whole map. Clearing
        // here would be the very failure the map replaced — the record
        // for the pilot this exists to recognise, thrown away because an
        // eighth stranger knocked. Ordered by an arrival counter and not
        // by remaining TTL: the TTL only ticks down inside the resolver's
        // gated loop, so entries filed before that gate opens all hold
        // the same value, and "smallest TTL" would silently mean "first
        // in map order" — which is lexicographic over decimal jids, where
        // "10" precedes "2".
        if (net_pending_joins_.size() >= 8) {
          std::map<std::string, PendingJoin>::iterator oldest =
              net_pending_joins_.begin();
          for (std::map<std::string, PendingJoin>::iterator pit =
                   net_pending_joins_.begin();
               pit != net_pending_joins_.end(); ++pit)
            if (pit->second.arrival < oldest->second.arrival) oldest = pit;
          net_pending_joins_.erase(oldest);
        }
        PendingJoin pj;
        pj.ttl_ms = NET_JOIN_IDENT_WAIT_MS;
        pj.arrival = net_pending_join_seq_++;
        net_pending_joins_[ev.peer] = pj;
        NET_LOG("net: peer joined (jid %s) while seat %d is mid-handshake - "
                "identifying\n", ev.peer.c_str(), (int)hs->seat);
      }
    }
  }

  net_host_rejoin_flap_check(delta);

  // Eaten-offer watchdog (N>1): the door's unaddressed offer is handed to
  // whichever joiner socket is oldest AT DELIVERY time and consumed on
  // delivery (worker.js) — and right after an N>1 rejoin completes, that
  // can be the JUST-SEATED client's socket still draining toward close,
  // which eats the re-armed door's offer: the next rejoiner then joins an
  // empty slot and waits forever. 2P never re-arms into that window (its
  // one seat is refilled). An offer nobody answers within the window is
  // an offer nobody HOLDS — re-push the same still-unanswered SDP and
  // the worker hands it to the rejoiner actually waiting (or stores it
  // for the next arrival). Never fires mid-handshake: an answered offer
  // has a session (net_handshaking_lost_peer), so this cannot double-
  // offer a client whose exchange is merely slow — the failure mode a
  // re-offer-on-PeerJoin variant of this fix actually hit (the second
  // offer tore down the client's in-flight answer).
  if (net_rehost_ && net_rehost_offer_sent_ && !net_handshaking_lost_peer()) {
    net_rehost_offer_age_ms_ += delta;
    if (net_rehost_offer_age_ms_ > 6000) {
      NET_LOG("net: rejoin offer unanswered - re-pushing\n");
      // Re-arm the top-of-poll send rather than pushing directly: that
      // path follows the offer with the cached candidate replay, so the
      // rejoiner actually waiting gets a completable exchange (the bare
      // SDP alone strands a TURN-only client until ICE times out).
      net_rehost_offer_sent_ = false;
      net_rehost_offer_age_ms_ = 0;
    }
  } else {
    net_rehost_offer_age_ms_ = 0;
  }
}

// Fresh session handshaking (HELLO/WELCOME) over whichever door's
// transport was adopted (relay rehost or LAN re-pair) — called once per
// tick from the loss branch, after both door polls. A policy-refused
// rejoiner never reaches Ready — the session itself rejects inside the
// handshake (net_session.cpp, RejectNotAllowed) and lands in the
// Failed/Rejected branch below, which re-offers so the room stays open
// for an allowed rejoiner; the refused peer got an honest MSG_REJECT and
// its lobby stops retrying.
// Rejoin-by-identity (FOURPLAYER.md, closing a B5 known limit): the doors
// serve the LOWEST parked seat, so two simultaneous drops rejoining in the
// other order swapped hulls — scores, lives, tint, the lot. The HELLO
// claim names the pilot: when it matches exactly ONE parked peer's
// remembered identity (name AND platform, both sanitized on their
// respective receives), the WELCOME seats the rejoiner THERE instead.
// Claim-level deliberately: the room code already gates entry, the choice
// is only ever among PARKED seats, and the alternative was order-of-return
// seating — no less spoofable, just less correct. No renderable name, no
// match, or an ambiguous match (two parked seats remembering the same
// name) keeps the door's pick, the old behaviour.
int GLGame::net_rejoin_seat_for_identity(const NetIdentity &claimed) const {
  if (claimed.name.empty()) return 0;
  int match = 0;
  for (NetPeer *p : net_peers_) {
    if (!p->lost || !p->parked) continue;
    if (p->identity.name.empty()) continue;
    if (p->identity.name != claimed.name) continue;
    if (p->identity.platform != claimed.platform) continue;
    if (match) return 0;  // ambiguous — keep the door's seat
    match = (int)p->seat;
  }
  return match;
}

// O4: a rejoin attempt that half-establishes and dies (a blip mid-ICE, the
// app relaunched during the handshake) leaves the host holding an adoption
// session nobody is party to. The door will not re-arm while one exists
// (net_handshaking_lost_peer gates it), so that seat waits out the ~30 s
// ICE timeout — and because the door serves one seat at a time, so does
// everyone queued behind it.
//
// The corpse is identifiable the moment its owner comes back: both its
// socket and the new one announce an identity, and the worker relays each
// stamped with its jid. One person cannot be two joiners, so if the socket
// mid-handshake and the socket that just arrived carry the same pilot, the
// older one is a corpse.
//
// Four guards, and every one exists to protect a HEALTHY exchange from
// being mistaken for that corpse:
//  - a DIFFERENT pilot changes nothing here. Whatever seat they are
//    heading for, their turn comes when this adoption completes or times
//    out (the door is serialized — see net_door_peer; serving parked seats
//    concurrently is a separate change).
//  - the join must be on a different jid than the adoption itself. A
//    verified upgrade for the LIVE handshake's own socket lands late and
//    routinely, and names exactly this pilot: without the jid test this
//    branch would tear down the healthy exchange that attestation
//    describes, which is precisely the regression the N=1 branch's comment
//    is a monument to.
//  - the adoption must HAVE a jid, i.e. be the relay door's and not the
//    LAN door's — see the empty-jid note in the body, which the first e2e
//    run turned from a theory into a certainty.
//  - either socket still nameless means no comparison is possible, so the
//    case falls through to the deadline and the ICE timeout — today's
//    behaviour exactly. This can only make the flap case faster; it has no
//    path that makes anything slower.
void GLGame::net_host_rejoin_flap_check(int delta) {
  if (net_pending_joins_.empty()) return;
  NetPeer *hs = net_handshaking_lost_peer();
  if (!hs || hs->jid.empty()) {
    // Nothing mid-handshake (it completed or timed out), or the adoption
    // in flight is the LAN door's — no jid (net_install_admit_check).
    //
    // The LAN case is not hypothetical: both doors open on every loss, and
    // a rejoining pilot races them BOTH — its client rejoins the room over
    // the relay while the host's beacon offers it a LAN pairing. If the
    // LAN side adopts first, the relay join that follows is the same
    // person arriving by the other road, and an empty jid compares
    // unequal to every real one, so without this the branch would drop a
    // perfectly healthy LAN handshake and call it a corpse. A relay join
    // is evidence about relay adoptions only.
    //
    // Both cases discard the records, which is the whole lifetime rule:
    // a pending join is kept only while a relay adoption it might explain
    // is actually in flight. Anything else — no adoption, a LAN one, a
    // later loss episode — and it is stale evidence about a room that has
    // moved on, which is the one way this mechanism could reach a healthy
    // handshake it knows nothing about.
    net_pending_joins_.clear();
    return;
  }
  // Is this adoption actually dead? Ask the transport first and the clock
  // second, because the clock alone cannot tell a corpse from a slow
  // success. adopt_ms runs from session creation, so it covers ICE and
  // DTLS, and a strict room adds up to ADMIT_WAIT_MS of AdmitWait while
  // the worker's attestation lands — a healthy TURN rejoin can sit at
  // five to eight seconds without anything being wrong.
  //
  // A corpse cannot fake connected(): its ICE never completes, so the
  // data channel never opens. Everything slow-but-real — relayed
  // candidates, a held WELCOME — happens on the far side of that flag,
  // which is why it and not the stopwatch is the discriminator. The age
  // is only a backstop for a connection still legitimately negotiating.
  //
  // This pair is also what keeps the claim-level identity match below
  // from being a denial of service. "The worst a liar achieves is what
  // the ICE timeout does anyway" is true of a DEAD handshake and false
  // of a healthy one: with no liveness test, anyone holding the room
  // code could name themselves after the pilot currently rejoining and
  // kill their working attempt, repeatedly. Here a liar can only reach a
  // socket that never connected and has had long enough to — which is
  // precisely the case this exists to clear up.
  const NetTransport *t = hs->session->transport();
  if (!t || t->connected() || hs->adopt_ms < FLAP_MIN_ADOPT_MS) return;

  // A note on who this can and cannot help. Matching needs a NAME on both
  // sides, and Game Center relays none, so an iOS pilot's flapped rejoin
  // is not recognised here and still costs the room the ICE timeout. That
  // is a limitation of name-matching, not a hole: platform alone would
  // match every other iOS peer in the room, which is worse than waiting.
  //
  // Compare PILOTS, not seats. The obvious test — "does this joiner
  // resolve to the seat the adoption is on?" — is wrong, and wrong in a way
  // no single-parked-seat test can see, since with one seat parked the two
  // questions always answer alike (nseat_rejoin_flap_swap.sh parks two, and
  // is red on a build that asks the wrong one). The
  // door always offers the LOWEST parked seat, but the WELCOME re-maps the
  // session onto whichever seat the claim matches
  // (NetSession::seat_resolver_, #447): with seats 3 and 4 both parked and
  // seat 4's pilot answering seat 3's offer — nseat_swap's exact shape —
  // `hs` is seat 3's peer holding a handshake destined for seat 4, and
  // seat 3's pilot then rejoining would resolve to hs->seat and tear down
  // a perfectly healthy exchange belonging to someone else.
  //
  // Asking whether the SAME PILOT is on both sockets sidesteps the re-map
  // entirely: one person cannot be two joiners, so if the socket
  // mid-handshake and the socket that just arrived carry the same pilot,
  // the older one is a corpse whatever seat it was going to land on.
  //
  // Both of the adoption's own copies are taken at adoption time
  // (`attested` drained from the jid map, `adopt_claim` likewise), so
  // neither depends on a jid map that later overflows and clears.
  // Whether the worker vouched for this socket is a question about the
  // ATTESTATION, and it has to be asked before the fallback chain below
  // runs, not after. Game Center attests the ACCOUNT and relays no name
  // (game_center_verify.js), so an attested iOS socket has an empty
  // attested name and a CLAIMED alias sitting behind it — ask afterwards
  // and the fallback has already replaced the attestation with the claim,
  // and the like-for-like rule would let a liar match it.
  const bool mid_attested =
      hs->attested.platform_trust == NET_TRUST_ATTESTED ||
      hs->attested.name_trust == NET_TRUST_ATTESTED;
  NetIdentity mid = hs->attested;
  if (mid.name.empty()) mid = hs->adopt_claim;
  if (mid.name.empty()) mid = net_jid_identity(hs->jid);
  // One drop per seat per loss episode. The identity test below runs on
  // claims, and a claim is a self-report: someone with the room code can
  // name themselves after the pilot who is rejoining. The guards above
  // mean they can only reach a socket that never connected and has had
  // eight seconds to, but "rare and slow" is not "impossible" — so cap
  // the damage at one lost attempt instead of a loop. Cleared at park,
  // i.e. once per loss. The cost is that a genuine SECOND flap in the
  // same episode waits out its own timeout, which is the trade: the
  // first flap is the case with a field story.
  if (hs->flap_dropped) return;
  for (std::map<std::string, PendingJoin>::iterator it =
           net_pending_joins_.begin();
       it != net_pending_joins_.end();) {
    const std::string &jid = it->first;
    if (jid == hs->jid) {  // this pending join IS the adoption — never itself
      // Recorded against a DIFFERENT adoption (the record site checks
      // that), but adoptions come and go while an entry waits, and one
      // can end up owning the jid it was filed under. Re-testing here
      // rather than trusting the record makes the invariant true where
      // the decision is made: a socket is never evidence against itself,
      // whatever happened in between.
      net_pending_joins_.erase(it++);
      continue;
    }
    NetIdentity who = it->second.ident.name.empty() ? net_jid_identity(jid)
                                                    : it->second.ident;
    // Like for like: an ATTESTED socket's handshake may only be judged
    // against another attestation. Where the worker vouches for names —
    // Steam, Play Games, Game Center rooms — that shuts the spoofer out
    // completely, because they cannot mint the name they would need. An
    // unattested room keeps the claim-level match, which is the only
    // thing available there and still strictly better than the timeout.
    const bool who_attested = who.name_trust == NET_TRUST_ATTESTED;
    if (!who.name.empty() && !mid.name.empty() &&
        (!mid_attested || who_attested)) {
      if (who.name == mid.name && who.platform == mid.platform) {
        NET_LOG("net: pilot '%s' re-entered on jid %s while jid %s was still "
                "mid-handshake (%d ms, never connected) - dropping the stale "
                "adoption and re-offering\n",
                who.name.c_str(), jid.c_str(), hs->jid.c_str(), hs->adopt_ms);
        // The abandoned attempt's attestation goes with its session, as on
        // every other path that drops one: the re-entering joiner
        // re-announces on its fresh socket and re-attests through the
        // worker.
        hs->attested = NetIdentity();
        hs->adopt_claim = NetIdentity();
        hs->flap_dropped = true;
        net_drop_session(*hs);
        net_rehost_offer_sent_ = false;
        // Every record goes, not just this one: dropping the session
        // leaves no adoption in flight, which by the rule above is
        // exactly when they stop being evidence about anything. A second
        // flap in the same episode re-announces on its own new socket
        // and files a fresh record.
        net_pending_joins_.clear();
        return;
      }
      // Say so. This is the branch that protects an in-flight handshake
      // belonging to someone else, and until it logged, a test could only
      // assert the ABSENCE of the drop line — which is equally true of a
      // resolver that never ran at all. One line per record (the record
      // goes with it), so it cannot repeat per tick.
      NET_LOG("net: jid %s is pilot '%s', not '%s' who holds the handshake - "
              "leaving it alone\n",
              jid.c_str(), who.name.c_str(), mid.name.c_str());
      net_pending_joins_.erase(it++);  // a different pilot — leave them be
      continue;
    }
    // One side or the other is nameless so far. Keep waiting: an attested
    // name arrives later than a claim and comes from the platform, so it
    // can identify a socket a claim could not. If neither ever does, the
    // patience below hands the case back to the ICE timeout.
    it->second.ttl_ms -= delta;
    if (it->second.ttl_ms <= 0) net_pending_joins_.erase(it++);
    else ++it;
  }
}

void GLGame::net_install_admit_check(NetSession *s, int seat) {
  if (!s) return;
  s->set_admit_check([this, seat](const NetIdentity &claimed) {
    const NetPeer *p = net_peer_by_seat(seat);
    // The seat can't vanish under a live door, and if it somehow did the
    // session is about to be torn down with it — don't refuse on a guess.
    if (!p) return NetSession::AdmitAllow;
    // p->attested is what the worker said about the socket that answered
    // (cleared when the seat parked, so it can't be a previous pilot's),
    // and an empty jid is the LAN door — no worker, nothing to attest.
    return NetLobby::admit_verdict(claimed, p->attested, !p->jid.empty());
  });
}

void GLGame::net_host_rejoin_session_update(int delta) {
  // B5: the adoption in flight is the LOST peer holding a fresh session —
  // never a healthy peer's (their sessions aren't touched by the doors).
  NetPeer *dp = net_handshaking_lost_peer();
  if (dp) {
    dp->adopt_ms += delta;
    dp->session->update(delta);
    if (dp->session->phase() == NetSession::Ready) {
      // Ready means ADMITTED: the ban list and the anonymous-players policy
      // are enforced inside the handshake now (NetLobby::admit_verdict,
      // installed on the session when the door forms), shared with the
      // lobby's waiting room. The door used to check both here, after the
      // fact — refusing with a bare transport close (no reason for the
      // peer) and, for the anonymous check, with no grace at all: it
      // assumed the immediate re-offer would win the race against the
      // worker's async verdict that the first attempt had just lost. The
      // handshake holds instead. A refusal arrives at the Rejected branch,
      // which already drops and re-offers.
      // Rejoin-by-identity: the WELCOME may have promised a DIFFERENT
      // parked seat than the door pre-picked (peer_seat() is what the
      // resolver made it). Move the whole adoption — session, answering
      // jid, any early attestation — onto that peer before completing;
      // the door peer stays parked and the top-of-poll arm re-opens the
      // doors for it next tick.
      int rs = dp->session->peer_seat();
      if (rs != (int)dp->seat) {
        NetPeer *rp = net_peer_by_seat(rs);
        if (rp && rp != dp && rp->lost && !rp->session) {
          NET_LOG("net: rejoin identity-matched seat %d (door was %d)\n",
                  rs, (int)dp->seat);
          rp->session = dp->session;
          dp->session = nullptr;
          rp->jid = dp->jid;
          dp->jid.clear();
          rp->attested = dp->attested;
          dp->attested = NetIdentity();
          rp->adopt_claim = dp->adopt_claim;
          dp->adopt_claim = NetIdentity();
          // offline_paired deliberately NOT transferred: it belongs to a
          // seat's CURRENT pairing, and both seats' pairings are settled
          // below/on their own next adoption (the door peer stays parked
          // with its old state intact).
          if (rp == net_peer()) net_peer_jid_ = rp->jid;
          else if (dp == net_peer()) net_peer_jid_.clear();
          dp = rp;
        } else {
          // The promised seat closed between HELLO and Ready (its own
          // rejoin raced this one home). The client was already TOLD
          // that seat — adopting it onto the door seat would desync its
          // whole seat-keyed state — so drop and re-offer, the
          // Failed-branch treatment.
          NET_LOG("net: rejoin resolved seat %d no longer open - drop\n", rs);
          dp->attested = NetIdentity();
          net_drop_session(*dp);
          if (net_signal_) {
            net_rehost_ = NetTransport::create();
            net_rehost_cands_.clear();
            if (net_rehost_) {
              net_rehost_->set_trickle(true);
              net_rehost_->set_ice_servers(net_ice_);
              net_rehost_->start_host();
            }
            net_rehost_offer_sent_ = false;
          }
          net_lan_rejoin_reset();
          return;
        }
      }
      NetPeer &pr = *dp;
      GLShip *gs = player_by_seat(pr.seat);
      Ship *remote = gs ? gs->ship : NULL;
      pr.lost = false;
      pr.parked = false;
      // This seat's NEW pairing decides its offline display carve-out:
      // LAN door grants it, a relay re-pair clears it (that pairing
      // re-attests through the worker). Same 2P exclusion as the
      // constructor's roster rule — at one remote peer the global
      // context governs, so which door won a 2P doors-race cannot
      // change what renders.
      pr.offline_paired = net_rehost_adopt_lan_ && net_peers_.size() > 1;
      net_rejoin_parked_ = false;  // the next full loss pauses afresh
      // Both doors are satisfied: a stale relay offer transport (the LAN
      // door won) and the LAN door itself close down. The relay ROOM
      // stays — it is the durable identity for codes and invites. With
      // ANOTHER seat still parked, the arm blocks re-open both doors for
      // it on the next tick.
      if (net_rehost_) {
        net_rehost_->close();
        delete net_rehost_;
        net_rehost_ = nullptr;
      }
      net_lan_rejoin_reset();
      // A rejoin re-runs the handshake, so the identity re-arrived fresh —
      // refresh the stored badge (the rejoiner may be a different friend
      // dropping into the empty slot via the re-advertised invite). The
      // wire parse is claim-only: re-fold the worker attestation on top,
      // or the attested badge demotes to the role label right here. The
      // common ordering delivers the rejoiner's Event::Identity (worker
      // verify, ~300 ms) BEFORE p2p Ready (ICE, seconds) — and it was
      // cleared at park time, so a stale attestation can't label a
      // different friend; a verify that lands after Ready folds live via
      // net_host_signal_common_event as before.
      pr.identity = pr.session->peer_identity();
      net_apply_attested(pr.identity, pr.attested);
      // The OTHER clients' HUD rows track this seat's badge and carve-out
      // flag — re-relay now that both just settled (the mid-rejoin
      // attestation fold above defers its broadcast to here; the
      // rejoiner itself re-syncs on its first INPUT as before).
      net_broadcast_seat_identities();
      pr.have_input = false;      // re-baseline the one-shot counters
      pr.input_zeroed = false;
      pr.rtt_ms = -1.0f;          // fresh transport, fresh RTT baseline
      pr.ping_timer = 0;
      pr.rtt_ring_n = 0;
      pr.rtt_ring_i = 0;
      pr.held_suppress = 0xffff;  // fresh presses required, like a spawn
      net_force_keyframe_ = true;   // rejoined client starts from a keyframe
      pr.last_input_time = current_time;
      if (remote) {
        if (remote->is_alive()) {
          // Shield-parked hull: drop the parked shield to the normal
          // respawn grace window and resume in place — unless an
          // asteroid drifted onto the hull while its owner was away, in
          // which case resume at a clear spot instead of inside the
          // rock (try_current keeps the parked position when safe; the
          // rejoiner bootstraps from the next keyframe, so any move is
          // adopted as its baseline pose).
          remote->time_left_invincible = 1500;
          remote->safe_position(grid, true);
        } else {
          // Unpark directly — the step-countdown respawn would charge a life.
          remote->respawn(grid, false);
        }
        remote->bullets.clear();  // no lethal spawn-flash debris
      }
      net_set_generation_banner(generation);
      // Name the rejoiner when their identity is known ("GLENN
      // RECONNECTED"); a legacy peer gets its seat's role label.
      char role[16];
      snprintf(role, sizeof(role), "PLAYER %d", (int)pr.seat);
      net_banner_text_ =
          net_identity_name_or(pr.identity, role,
                               net_id_ctx_for_seat((int)pr.seat)) +
          " RECONNECTED";
      net_banner_ms_ = 3000;      // the JOINED/LEFT notices' duration
      NET_LOG("net: banner '%s' %d ms\n", net_banner_text_.c_str(), net_banner_ms_);
      net_banner_header_ = true;  // up top, same spot as DISCONNECTED
      // Re-sync the room rule — the rejoiner may be a fresh app launch
      // whose HUD reset to its own preference.
      net_send_event(Net::EV_FRIENDLY_FIRE, friendly_fire ? 1u : 0u);
      // Same "seat N" format as the ctor's adoption log — turnexpiry.sh
      // greps both lines for the relay/relay path.
      NET_LOG("net: ice path seat %d %s\n", (int)pr.seat,
             pr.session->transport()->connection_info().c_str());
      // Host paused (auto-paused on the disconnect, or by hand): the
      // paused tick never reaches the 10 Hz send, so push the keyframe
      // NOW — the rejoiner's lobby is waiting on it to bootstrap the
      // world. The EV_PAUSE follows on the client's first INPUT, once
      // it provably exists (the lobby does not consume events).
      if (!running) {
        net_snapshot_timer_ = 100;
        net_host_send_snapshot(0);
      }
      NET_LOG("net: player %d rejoined\n", (int)pr.seat);
      net_host_resume_persist();  // client-join checkpoint (see NETPLAY.md)
    } else if (dp->session->phase() == NetSession::Failed ||
               dp->session->phase() == NetSession::Rejected) {
      // Refused by this room's own policy (the ban list / anonymous
      // players — NetLobby::admit_verdict, run inside the handshake): the
      // REJECT naming the reason is queued but unflushed, so hand the
      // session to the drain instead of deleting it under the message.
      // net_drop_session below then no-ops on the detached pointer, and
      // the door re-arms this tick as it always did.
      if (dp->session->phase() == NetSession::Rejected &&
          dp->session->reject_reason() != 0) {
        NET_LOG("net: rejoin door refused seat %d (reason %u)\n",
                (int)dp->seat, (unsigned)dp->session->reject_reason());
        net_closing_.push_back(std::make_pair(dp->session, 600));
        dp->session = nullptr;
      }
      // Bad handshake (wrong build?): drop it and re-open the doors for
      // another try — the relay re-offer only where a signal exists, and
      // the LAN door reset so its poll re-arms a fresh beacon next tick.
      // The failed candidate's worker attestation dies with their session:
      // the park-time clear ran once per loss, so without this a verified
      // candidate whose handshake then failed would leave their attested
      // name to be folded onto whoever completes the NEXT attempt (an
      // unverified or legacy joiner would wear it at the Ready re-fold).
      dp->attested = NetIdentity();
      dp->adopt_claim = NetIdentity();  // same rule, same reason
      net_drop_session(*dp);
      if (net_signal_) {
        net_rehost_ = NetTransport::create();
        net_rehost_cands_.clear();
        if (net_rehost_) net_rehost_->set_trickle(true);
        net_rehost_offer_sent_ = false;
        if (net_rehost_) {
          net_rehost_->set_ice_servers(net_ice_);
          net_rehost_->start_host();
        }
      }
      net_lan_rejoin_reset();
    }
  }
}

// The LAN door's half of the loss handling: beacon + serve a fresh
// lan-only offer over TCP, adopt a completed re-pair. Runs beside the
// relay rejoin poll (both doors, like the lobby) or alone when the
// session came through the LAN door and there is no signal. Returns
// false when LAN discovery isn't available on this platform — with no
// signal either, the loss is terminal as before.
bool GLGame::net_host_lan_rejoin_poll(int delta) {
  // lan_visible off (INI-only pref) means this host never beacons — the
  // mid-game re-host door stays closed too, mirroring the lobby gate.
  if (net_mode_ != NetHost || !NetLan::available() || !g_prefs.lan_visible)
    return false;
  // Arm once per open seat — and NOT while a fresh session from either
  // door is already handshaking (re-beaconing then would let a second
  // completion stomp it; mirror of the relay arm's condition). Serves
  // the same lowest parked seat as the relay door (net_rehost_seat_ is
  // stamped by whichever arms first).
  NetPeer *door = net_door_peer();
  if (!net_lan_rehost_ && !net_lan_announce_.running() &&
      !net_handshaking_lost_peer() && door) {
    net_rehost_seat_ = door->seat;
    // Re-beacon the session's frozen name (set at the lobby hand-off) so
    // the dropped client's rediscover-by-name matches; never re-read
    // local_host_name() here — it can have drifted (iOS alias).
    if (net_lan_beacon_name_.empty())
      net_lan_beacon_name_ = NetLan::local_host_name();
    if (!net_lan_announce_.start(net_lan_beacon_name_)) return false;
    net_lan_rehost_ = NetTransport::create();
    net_lan_offer_set_ = false;
    if (net_lan_rehost_) {
      net_lan_rehost_->set_lan_only(true);  // host candidates only
      net_lan_rehost_->set_trickle(false);  // the blob carries everything
      net_lan_rehost_->start_host();
    }
    NET_LOG("net: player %d lost - lan door reopened for rejoin\n",
            (int)door->seat);
  }
  if (!net_lan_offer_set_ && net_lan_rehost_ &&
      net_lan_rehost_->local_description_ready()) {
    net_lan_announce_.set_offer_blob(
        Net::encode_signal(true, net_lan_rehost_->local_description()));
    net_lan_offer_set_ = true;
  }
  std::string answer;
  if (net_lan_announce_.update(delta, answer)) {
    std::string sdp;
    if (Net::decode_signal(answer, sdp) == 'A' && net_lan_rehost_) {
      NET_LOG("net: lan rejoiner completed - adopting\n");
      // The LAN door won this race. A relay rehost offer still out (or a
      // half-open session from a flapped earlier attempt) is stale — the
      // completed human is on THIS transport. The relay room itself
      // stays open via net_signal_.
      if (net_rehost_) {
        net_rehost_->close();
        delete net_rehost_;
        net_rehost_ = nullptr;
      }
      NetPeer *dp = net_peer_by_seat(net_rehost_seat_);
      if (!dp) dp = net_peer();  // belt & braces; the seat can't vanish
      if (dp) {
        net_drop_session(*dp);  // a half-open earlier attempt is stale
        net_lan_announce_.stop();
        net_lan_rehost_->set_remote_answer(sdp);
        dp->session =
            new NetSession(net_lan_rehost_, NetSession::HostRole, dp->seat);
        dp->adopt_ms = 0;
        dp->adopt_claim = NetIdentity();
        // And the attestation, for the relay door's reason plus one of its
        // own: nothing attests a LAN pairing, so a dropped relay attempt's
        // verdict left here would be worn — and broadcast to every client —
        // by whoever completes this local handshake instead.
        dp->attested = NetIdentity();
        // Same rejoin-by-identity resolver as the relay door.
        dp->session->set_seat_resolver([this](const NetIdentity &claimed) {
          return net_rejoin_seat_for_identity(claimed);
        });
        // ...and the same admission gate. The LAN door clears dp->jid just
        // below, so the anonymous policy self-disables here and only the
        // ban list bites — the door's long-standing behaviour.
        net_install_admit_check(dp->session, (int)dp->seat);
        // Paired through the local beacon: per-peer offline carve-out —
        // recorded on the adoption and applied at Ready to whichever seat
        // the resolver lands it on (a Failed handshake then leaves no
        // stray carve-out on the door guess, which used to let a parked
        // RELAY seat render its unattested claim).
        net_rehost_adopt_lan_ = true;
        dp->jid.clear();        // LAN door: no worker jid for this pairing
        net_lan_rehost_ = nullptr;  // owned by the session now
      }
    }
  }
  return true;
}

void GLGame::net_lan_rejoin_reset() {
  net_lan_announce_.stop();
  if (net_lan_rehost_) {
    net_lan_rehost_->close();
    delete net_lan_rehost_;
    net_lan_rehost_ = nullptr;
  }
  net_lan_offer_set_ = false;
}

// A rejoin door is open on the LAN side (announce beaconing, or a
// re-pair mid-exchange): the loss is recoverable, so the input handlers
// must not treat any key as "exit to menu" and the overlay shows the
// waiting notice instead of the terminal card.
bool GLGame::net_lan_door_open() const {
  return net_lan_announce_.running() || net_lan_rehost_ != nullptr;
}

// ---- snapshot NetExtras -------------------------------------------------
// Appended after the serialize_game body: per-ship transients the save
// format deliberately omits, the public projectile vectors, and the
// asteroid net_id array (same iteration order as build_save_data). Raw
// native-endian writes, matching the savegame body it rides with (every
// supported platform is little-endian).

namespace {

template <typename T>
void nx_write(Save::Stream &out, const T &v) { out.write(&v, sizeof(T)); }

void nx_write_projectile(Save::Stream &out, const Object &o) {
  nx_write(out, o.position.x());
  nx_write(out, o.position.y());
  nx_write(out, o.velocity.x());
  nx_write(out, o.velocity.y());
}

void nx_write_ship(Save::Stream &out, const Ship &s) {
  // PROTO 25: each ship record leads with its seat (the PROTO-16 enemy
  // net_ship_id pattern), so receivers key records by seat instead of
  // list position. Replay files recorded pre-v19 carry no seat byte —
  // the reader gates on the GameState's seats being stamped.
  nx_write(out, s.net_seat);
  nx_write(out, (uint8_t)s.is_alive());
  nx_write(out, s.temperature);
  nx_write(out, (int32_t)s.time_until_respawn);
  nx_write(out, (int32_t)s.time_left_invincible);
  nx_write(out, (int32_t)s.god_mode_time_remaining());
  nx_write(out, (uint8_t)s.shield_active());
  nx_write(out, s.net_warp_count);
  // Movement flags so the peer can run this ship's exhaust-trail
  // emitters: bit0 thrust, bit1 reverse, bits 2-3 rotation (1=L, 2=R).
  uint8_t move = (uint8_t)((s.thrusting ? 1 : 0) | (s.reversing ? 2 : 0) |
                           ((s.rotation_direction == Ship::LEFT    ? 1
                             : s.rotation_direction == Ship::RIGHT ? 2
                                                                   : 0)
                            << 2));
  nx_write(out, move);

  nx_write(out, (uint16_t)s.bullets.size());
  for (const Particle &p : s.bullets) {
    nx_write_projectile(out, p);
    // PROTO 18: per-bullet flags so piercing (beam bolt), trail and
    // kills_invincible survive the wholesale 10 Hz rebuild — a host beam
    // bolt used to flicker back to a plain pew on every apply.
    nx_write(out, p.net_flags());
  }
  nx_write(out, (uint16_t)s.mines.size());
  for (const Particle &p : s.mines) nx_write_projectile(out, p);
  nx_write(out, (uint16_t)s.giga_mines.size());
  for (const Particle &p : s.giga_mines) nx_write_projectile(out, p);

  nx_write(out, (uint16_t)s.missiles.size());
  for (const MissileShot &m : s.missiles) {
    nx_write_projectile(out, m);
    nx_write(out, m.facing.x());
    nx_write(out, m.facing.y());
    nx_write(out, m.time_left);
  }

  nx_write(out, (uint16_t)s.shockwaves.size());
  for (const Shockwave &w : s.shockwaves) {
    nx_write(out, w.position.x());
    nx_write(out, w.position.y());
    nx_write(out, w.radius);
    nx_write(out, w.max_radius);
    nx_write(out, w.speed);
    nx_write(out, w.time_left);
    nx_write(out, (uint8_t)w.is_nova);
  }

  // v20 (PROTO 26): deployed turrets. Readers gate this section on the
  // stream's save version (nx_read_projectiles) so pre-v20 replay files
  // still parse; the live wire is PROTO-fenced to matching builds.
  nx_write(out, (uint16_t)s.turrets.size());
  for (const TurretDrone &t : s.turrets) {
    nx_write_projectile(out, t);
    nx_write(out, t.aim);
    nx_write(out, t.ms_left);
    nx_write(out, t.fire_cooldown);
    nx_write(out, (uint8_t)(t.shots_left < 0 ? 0 : t.shots_left));
  }
}

// v6: the mini-station's shots — its Save record carries none, so the
// client faced invisible (and lethal-looking) fire at generation 10+.
void nx_write_mini_station_bullets(Save::Stream &out, const GLMiniStation *ms) {
  uint16_t n = (ms && ms->is_alive()) ? (uint16_t)ms->bullets.size() : 0;
  nx_write(out, n);
  if (ms)
    for (uint16_t i = 0; i < n; i++) nx_write_projectile(out, ms->bullets[i]);
}

// v7: the gen-20 station's deployed enemies' shots, in enemies-list order
// (the client rebuilds its enemies from the station record in the same
// order every apply). PROTO 16: each record leads with the enemy's
// net_ship_id — the rebuilt replicas' only durable identity, re-stamped
// on the client every apply, referenced by exact MSG_HIT_SHIP claims.
void nx_write_enemy_bullets(Save::Stream &out, const std::list<GLShip *> *enemies) {
  nx_write(out, (uint16_t)enemies->size());
  for (auto *e : *enemies) {
    nx_write(out, e->ship->net_ship_id);
    nx_write(out, (uint16_t)e->ship->bullets.size());
    for (const Particle &b : e->ship->bullets) nx_write_projectile(out, b);
  }
}

}  // namespace

// One state byte per asteroid in delta records: the transient flags a
// client cannot extrapolate (motion and rotation it can — see NETPLAY.md).
static uint8_t ast_state_byte(const Asteroid *a) {
  return (uint8_t)((a->phased ? 1 : 0) | (a->teleport_vulnerable ? 2 : 0) |
                   (a->teleport_pending ? 4 : 0) |
                   (a->quantum_observed ? 8 : 0));
}

void GLGame::net_host_send_snapshot(int delta) {
  // Mid-rejoin (peer gone, room still open) the world plays on — and so
  // must an online recording: keep the cadence and the builders running so
  // the file has no hole, just skip the sends. The rejoined client restarts
  // from the forced keyframe anyway, which rebuilds its baseline.
  // PB-D2: ONE build, byte-identical broadcast — a peer either receives
  // every slot since its keyframe or gets forced a fresh global keyframe
  // (join/rejoin set net_force_keyframe_). can_send = some peer can take
  // it; the per-peer liveness picks receivers at the write below.
  bool can_send = false;
  for (NetPeer *p : net_peers_)
    if (p->session && !p->lost) { can_send = true; break; }
  if (!can_send && !replay_) return;  // mid-rejoin, nothing recording
  net_snapshot_timer_ += delta;
  if (net_snapshot_timer_ < 100) return;
  net_snapshot_timer_ = 0;

  bool keyframe = net_force_keyframe_ || (net_slot_ % 10 == 0);
  net_slot_++;
  if (!keyframe && net_send_delta(can_send)) return;

  // KEYFRAME: the full snapshot, exactly as Milestone 1 sent every slot.
  Save::MemStream payload;
  net_build_keyframe_payload(payload);

  // Replay tee (host-side online recording): the exact bytes the client
  // is fed become the file's KEYFRAME record — never a second build.
  if (replay_) replay_->record_keyframe(payload.data());

  if (can_send) {
    ++net_snapshot_id_;  // one room-level id per slot, shared by all peers
    for (NetPeer *p : net_peers_)
      if (p->session && !p->lost)
        Net::send_snapshot(p->session->transport(), net_snapshot_id_,
                           payload.data(), 1);

    // Bandwidth telemetry (M2-6): a line every 10 s of play at 10 Hz.
    net_bytes_sent_ += payload.data().size();
    if (net_slot_ % 100 == 0) {
      NET_LOG("net: slot #%d gen=%d asteroids=%d key_bytes=%d avg10s=%.1f KB/s\n",
             net_slot_, generation, (int)objects->size(),
             (int)payload.data().size(), net_bytes_sent_ / 10240.0f);
      net_bytes_sent_ = 0;
    }
  }
  net_force_keyframe_ = false;
}

// REPLAY.md R1 seam: the payload builders below are shared by the online
// host (net_host_send_snapshot / net_send_delta) and the replay recorder
// (replay_record_slot) — they must never fork. Both mutate net_known_, the
// delta baseline; that sharing is safe because only one CALLER is ever
// active: offline the recorder's cadence drives them, online only the host
// send path does and the recorder tees the built bytes from inside it (a
// second build per slot would corrupt the other consumer's baseline).

void GLGame::net_build_keyframe_payload(Save::MemStream &payload) {
  Save::serialize_game(payload, build_save_data());

  nx_write(payload, (uint32_t)players->size());
  for (auto *gs : *players) nx_write_ship(payload, *gs->ship);
  nx_write_mini_station_bullets(payload, mini_station);
  nx_write_enemy_bullets(payload, enemies);

  nx_write(payload, (uint32_t)objects->size());
  for (auto *a : *objects) nx_write(payload, a->net_id);

  // The keyframe is the consumer's new baseline: deltas from here describe
  // changes against it (reliable ordered channel / in-order file reads —
  // no acks needed).
  net_known_.clear();
  for (auto *a : *objects) {
    NetAstBase b;
    b.px = a->position.x(); b.py = a->position.y();
    b.vx = a->velocity.x(); b.vy = a->velocity.y();
    b.health = (uint8_t)a->health;
    b.state = ast_state_byte(a);
    b.t = current_time;
    net_known_[a->net_id] = b;
  }
}

bool GLGame::net_build_delta_payload(Save::MemStream &payload, int counts[3]) {
  // Everything except asteroids rides wholesale — it is small and reuses
  // the entire keyframe apply path on the client. Asteroids are diffed
  // below, so skip capturing them into s.
  Save::GameState s = build_save_data(false);
  Save::serialize_game(payload, s);

  nx_write(payload, (uint32_t)players->size());
  for (auto *gs : *players) nx_write_ship(payload, *gs->ship);
  nx_write_mini_station_bullets(payload, mini_station);
  nx_write_enemy_bullets(payload, enemies);

  // Asteroids: new since the last keyframe/delta, changed beyond what the
  // client can extrapolate, or gone. Record every live id in this single
  // pass so the removed set is the known ids NOT seen — the old nested
  // "for each known, scan all objects" was O(N^2) at 9x/s.
  std::vector<Asteroid *> fresh, dyn;
  std::set<uint32_t> present_ids;
  for (auto *a : *objects) {
    present_ids.insert(a->net_id);
    std::map<uint32_t, NetAstBase>::iterator f = net_known_.find(a->net_id);
    if (f == net_known_.end()) {
      fresh.push_back(a);
      continue;
    }
    const NetAstBase &b = f->second;
    float dt = (float)(current_time - b.t);
    float pred_x = b.px + b.vx * dt, pred_y = b.py + b.vy * dt;
    bool dirty =
        fabsf(a->velocity.x() - b.vx) > 0.01f ||
        fabsf(a->velocity.y() - b.vy) > 0.01f ||
        (uint8_t)a->health != b.health || ast_state_byte(a) != b.state ||
        // 16, not 50: elastic separation pushes move POSITION without
        // touching velocity, so rocks resting in contact piles drifted
        // silently to the old threshold, corrected, and drifted again —
        // a permanent ~50-unit sawtooth on every touching pair (the
        // "constant position jitter"). A 16-unit correction glides in
        // below perception; the extra records only flow while a pile is
        // actually grinding.
        fabsf(a->position.x() - pred_x) > 16.0f ||
        fabsf(a->position.y() - pred_y) > 16.0f;
    if (dirty) dyn.push_back(a);
  }
  std::vector<uint32_t> removed;
  for (std::map<uint32_t, NetAstBase>::iterator it = net_known_.begin();
       it != net_known_.end(); ++it)
    if (present_ids.find(it->first) == present_ids.end())
      removed.push_back(it->first);

  nx_write(payload, (uint16_t)fresh.size());
  for (size_t i = 0; i < fresh.size(); i++) {
    nx_write(payload, fresh[i]->net_id);
    Save::write_asteroid(payload, fresh[i]->capture_state());
  }
  nx_write(payload, (uint16_t)dyn.size());
  for (size_t i = 0; i < dyn.size(); i++) {
    Asteroid *a = dyn[i];
    nx_write(payload, a->net_id);
    nx_write(payload, a->position.x());
    nx_write(payload, a->position.y());
    nx_write(payload, a->velocity.x());
    nx_write(payload, a->velocity.y());
    nx_write(payload, (uint8_t)a->health);
    nx_write(payload, ast_state_byte(a));
  }
  nx_write(payload, (uint16_t)removed.size());
  for (size_t i = 0; i < removed.size(); i++) nx_write(payload, removed[i]);

  // Rare escape hatch: a delta bigger than one snapshot chunk (mass
  // change) is not worth the format — the caller does a keyframe this slot
  // instead. The baseline is untouched, so nothing was promised.
  if (payload.data().size() > Net::SNAPSHOT_CHUNK_BYTES) return false;

  // The payload is committed (the host's send_reliable and the recorder's
  // chunk append cannot fail detectably from here) — move the baseline.
  for (size_t i = 0; i < removed.size(); i++) net_known_.erase(removed[i]);
  std::vector<Asteroid *> *sent[2] = { &fresh, &dyn };
  for (int k = 0; k < 2; k++)
    for (size_t i = 0; i < sent[k]->size(); i++) {
      Asteroid *a = (*sent[k])[i];
      NetAstBase b;
      b.px = a->position.x(); b.py = a->position.y();
      b.vx = a->velocity.x(); b.vy = a->velocity.y();
      b.health = (uint8_t)a->health;
      b.state = ast_state_byte(a);
      b.t = current_time;
      net_known_[a->net_id] = b;
    }

  if (counts) {
    counts[0] = (int)fresh.size();
    counts[1] = (int)dyn.size();
    counts[2] = (int)removed.size();
  }
  return true;
}

bool GLGame::net_send_delta(bool can_send) {
  Save::MemStream payload;
  int counts[3] = {0, 0, 0};
  if (!net_build_delta_payload(payload, counts)) return false;

  // Replay tee (host-side online recording) — the payload is committed
  // (the baseline moved), so it must land in the file even when the send
  // is skipped (mid-rejoin: can_send false, the recording plays on).
  if (replay_) replay_->record_delta(payload.data());
  if (!can_send) return true;

  // PB-D2: one encode, byte-identical to every live peer (one id per slot).
  std::vector<uint8_t> msg;
  msg.reserve(Net::HEADER_SIZE + 4 + payload.data().size());
  Net::put_header(msg, Net::MSG_DELTA, 1);
  Net::put_u32(msg, ++net_snapshot_id_);
  Net::put_bytes(msg, &payload.data()[0], payload.data().size());
  for (NetPeer *p : net_peers_)
    if (p->session && !p->lost)
      p->session->transport()->send_reliable(&msg[0], msg.size());

  net_bytes_sent_ += msg.size();
  if (net_slot_ % 100 == 0) {
    NET_LOG("net: slot #%d gen=%d asteroids=%d delta_bytes=%d (new %d dyn %d rm %d) avg10s=%.1f KB/s\n",
           net_slot_, generation, (int)objects->size(), (int)msg.size(),
           counts[0], counts[1], counts[2],
           net_bytes_sent_ / 10240.0f);
    net_bytes_sent_ = 0;
  }
  return true;
}

// ---- replay recording (REPLAY.md R1) ----

// Every solo game records, cheat-flagged or not (the flag rides the header;
// a cheat run can be recent but never best). Resume: a save whose run_id
// matches current.nrp's header continues that recording (seam keyframe,
// continuous slots); anything else rotates the leftover current into recent
// first so an abandoned-forever run is never silently lost.
//
// Online games record too — BOTH roles, each machine writing what its own
// screen was fed (the host tees the snapshots it builds and sends; the
// client tees the stream it receives, so its own ship shows as the host's
// reconciled view of it). The file is replays/online.nrp — the REPLAYS
// menu's ONLINE RUN row — deliberately separate from current.nrp so
// hosting/joining mid-way through an offline run never rotates that run's
// resumable recording away. It never rotates anywhere: each new online
// session overwrites it (like each offline run overwrites recent), with a
// best check on the cleanly-closed leftover being replaced. run_id rides
// every snapshot (GameState v17), so a client whose auto-rejoin rebuilt
// the GLGame finds the same run_id in the leftover file and APPENDS — the
// self-built seam keyframe below keeps the timeline continuous, the
// disconnect gap simply compresses out (slots are emission counts, not
// wall clock).
void GLGame::replay_start() {
  // Opt-in: recording is OFF by default (REPLAY.md ship posture). The
  // preference or NEWTONIA_REPLAY_ENABLE (tests/CI, power users) turns it
  // on; NEWTONIA_REPLAY_DISABLE forces it off and wins. Resolved at game
  // start so a mid-run change never orphans a half-written file — an
  // existing current.nrp is simply left untouched.
  int override_ = Replay::recording_override();
  bool enabled = override_ >= 0 ? override_ == 1 : g_prefs.auto_record_replays;
  if (override_ >= 0)
    SDL_Log("replay: NEWTONIA_REPLAY_%s overrides the preference (%s)",
            override_ == 1 ? "ENABLE" : "DISABLE", enabled ? "ON" : "OFF");
  if (!enabled) return;
  if (net_mode_ == NetHost || net_mode_ == NetClient) {
    // The CLIENT records under a DERIVED id (bitwise NOT of the shared
    // run_id_): both peers record the same session, but online co-op
    // board entries are PERSONAL claims — two accounts, two rows
    // (LEADERBOARD.md, decided 2026-08-02) — and the worker keys rows by
    // run_id, so the two files must not share one. The derivation is
    // deterministic (a rejoin re-receives the host's run_id and derives
    // the same value, so resume still matches) and collision-safe
    // against random 64-bit ids. run_id_ itself — saves, snapshots, the
    // wire — stays the host's everywhere else.
    uint64_t rec_id = net_mode_ == NetClient ? ~run_id_ : run_id_;
    if (rec_id == 0) rec_id = 1;  // ~x is 0 only for x = UINT64_MAX
    Replay::Header h;
    bool resumed = Replay::read_header(Replay::online_path(), h) &&
                   run_id_ != 0 && h.run_id == rec_id &&
                   // Same version fence as the offline resume below.
                   h.save_version == Save::GameState::VERSION &&
                   h.format_version == Replay::Header::FORMAT_VERSION;
    // About to overwrite the previous session: give a cleanly-closed
    // leftover its best check first (the twin of on_new_game's offline
    // rotation check). Ended runs were checked at finalize — repeating
    // the check is an idempotent no-op.
    if (!resumed) Replay::best_check_online();
    replay_ = new Replay::Recorder(rec_id, (uint8_t)players->size(),
                                   resumed, Replay::online_path());
    if (!replay_->ok()) {
      delete replay_;
      replay_ = nullptr;
      return;
    }
    // The client's wire stream has no opening record: the lobby consumed
    // the bootstrap keyframe before this game existed, so it never reached
    // the file. This used to substitute a keyframe built from the freshly
    // bootstrapped replica — but the client records the HOST's keyframes
    // and deltas VERBATIM, so every delta is encoded against a host
    // keyframe, and a replica-derived stand-in is not that baseline. It is
    // close (the replica came from the bootstrap) but not equal, and the
    // gap never closes: objects a delta does not mention keep the error
    // until the next full host keyframe re-seeds it, one second later,
    // forever. Field-measured on an Android client rejoin (2026-07-27):
    // ~2 corrections/s at ~10 units before the seam, ~20/s at 50-70 units
    // for the whole 100 s after it — every one under the snap threshold,
    // so net_smooth_step glided them and the world visibly slid once a
    // second.
    //
    // Wait for a REAL host keyframe instead (recorded by the net_client_poll
    // tee, at most one second out). Costs those records; buys a file whose
    // every baseline is authority's. A resumed recorder must be re-armed
    // explicitly — it starts satisfied by the leftover's keyframe, which
    // belongs to the PREVIOUS session and is just as wrong a baseline for
    // this one's deltas.
    if (net_mode_ == NetClient) replay_->await_keyframe();
    net_force_keyframe_ = true;
    return;
  }
  bool resumed = false;
  if (replay_resume_candidate_) {
    Replay::Header h;
    resumed = Replay::read_header(Replay::current_path(), h) &&
              h.run_id == run_id_ &&
              // Version fence: the reader parses EVERY record with the
              // header's save_version, so appending this build's records to
              // a file serialized under an older savegame format would mix
              // versions it can't distinguish — the appended tail would
              // silently drop on playback. A version-bumped build starts a
              // fresh recording instead; the old segment rotates into
              // recent, still watchable best-effort (REPLAY.md seasons).
              h.save_version == Save::GameState::VERSION &&
              h.format_version == Replay::Header::FORMAT_VERSION;
  }
  if (!resumed) Replay::on_new_game();
  replay_ = new Replay::Recorder(run_id_, (uint8_t)players->size(), resumed,
                                 Replay::current_path());
  if (!replay_->ok()) {
    delete replay_;
    replay_ = nullptr;
    return;
  }
  // First record must be a keyframe: fresh-file bootstrap or resume seam.
  // Already true at construction; explicit for the resume-append case.
  net_force_keyframe_ = true;
}

// One KEYFRAME/DELTA per 100 ms of running sim, mirroring the host's
// cadence (the call site is only reached while running, so pauses emit
// nothing and the slot timeline is pure play time). Keyframes land every
// 10th slot, plus wherever net_force_keyframe_ demands one (start, resume
// seam, level rebuild).
void GLGame::replay_record_slot(int delta) {
  if (!replay_) return;
  replay_slot_timer_ += delta;
  if (replay_slot_timer_ < 100) return;
  replay_slot_timer_ = 0;

  // Keep the recorder's idea of the run current, so a flush that lands
  // without a finalize behind it (crash, killed tab) still leaves an
  // honest header — see Recorder::patch_header_tail.
  uint32_t best = 0;
  for (auto *gs : *players) {
    int s = gs->ship->score;
    if (s > 0 && (uint32_t)s > best) best = (uint32_t)s;
  }
  replay_->note_progress(best, (uint32_t)(generation < 0 ? 0 : generation));

  if (!net_force_keyframe_ && !replay_->keyframe_due()) {
    Save::MemStream payload;
    if (net_build_delta_payload(payload)) {
      replay_->record_delta(payload.data());
      return;
    }
  }
  Save::MemStream payload;
  net_build_keyframe_payload(payload);
  replay_->record_keyframe(payload.data());
  net_force_keyframe_ = false;
}

// Drain the Ship::replay_* effect outboxes. The bodies reuse the MSG wire
// encodings (u8 count | f32 pairs) so playback feeds the exact receive
// functions the net client uses; rings carry their Shockwave parameters.
// Attribution: the pushing Ship* resolves to a player index at record time
// (effects from non-player ships — there are none today — would drop).
int GLGame::player_index_of(const Ship *s) const {
  int i = 0;
  for (auto *gs : *players) {
    if (gs->ship == s) return i;
    i++;
  }
  return -1;
}

// Encode a lance/shock polyline as a REC_EFFECT body (the MSG wire shape:
// u8 count | count * (f32 x, f32 y)). Shared by the outbox drain (the local
// player's fired visuals) and the receive-site tees (the remote player's,
// which arrive as MSG_LANCE/MSG_SHOCK and never touch the outboxes). The
// bounds match the per-weapon wire limits playback's receive validators
// enforce (lance 17, shock 15) — an oversized local bolt would only waste
// an unplayable record.
void GLGame::replay_record_polyline(uint8_t subtype, const Ship *shooter,
                                    const std::vector<Point> &pts) {
  size_t max_pts = subtype == Replay::FX_LANCE ? 17 : 15;
  if (!replay_ || pts.size() < 2 || pts.size() > max_pts) return;
  int idx = player_index_of(shooter);
  if (idx < 0) return;
  std::vector<uint8_t> body;
  Net::put_u8(body, (uint8_t)pts.size());
  for (const Point &p : pts) {
    Net::put_f32(body, p.x());
    Net::put_f32(body, p.y());
  }
  replay_->record_effect(subtype, (uint8_t)idx, body);
  NET_LOG("replay: effect %s recorded (p%d, %d pts)\n",
          subtype == Replay::FX_LANCE ? "lance" : "shock", idx + 1,
          (int)pts.size());
}

// One FX_SHOT sound cue (f32 x, f32 y | u8 kind: 0 pew, 1 beam). Shared by
// the outbox drain and the MSG_SHOT receive tees.
void GLGame::replay_record_shot(float x, float y, uint8_t kind) {
  if (!replay_) return;
  std::vector<uint8_t> body;
  Net::put_f32(body, x);
  Net::put_f32(body, y);
  Net::put_u8(body, kind);
  replay_->record_effect(Replay::FX_SHOT, 0, body);
}

void GLGame::replay_drain_effects() {
  if (replay_) {
    for (auto &lf : Ship::replay_lance_flashes)
      replay_record_polyline(Replay::FX_LANCE, lf.first, lf.second);
    for (auto &sf : Ship::replay_shock_flashes)
      replay_record_polyline(Replay::FX_SHOCK, sf.first, sf.second);
    for (auto &rg : Ship::replay_rings) {
      int idx = player_index_of(rg.ship);
      if (idx < 0) continue;
      std::vector<uint8_t> body;
      Net::put_f32(body, rg.x);
      Net::put_f32(body, rg.y);
      Net::put_f32(body, rg.max_r);
      Net::put_f32(body, rg.speed);
      Net::put_f32(body, rg.duration);
      Net::put_u8(body, rg.nova ? 1 : 0);
      replay_->record_effect(Replay::FX_RING, (uint8_t)idx, body);
      NET_LOG("replay: effect ring recorded (p%d, %s)\n", idx + 1,
              rg.nova ? "nova" : "giga");
    }
    for (const Point &p : Ship::replay_pews)
      replay_record_shot(p.x(), p.y(), 0);  // sound kind: pew
    for (const Point &p : Ship::replay_beam_pews)
      replay_record_shot(p.x(), p.y(), 1);  // sound kind: beam
    // v2: one clone record per bullet, stamped with its owning player.
    // Enemy / mini-station shots have no player index and keep riding the
    // snapshots — nobody watches a bullet leave THEIR nose, and the sound
    // cue above already covers them.
    for (const auto &rs : Ship::replay_shots) {
      int idx = player_index_of(rs.ship);
      if (idx < 0) continue;
      std::vector<uint8_t> body;
      Net::put_f32(body, rs.pos.x());
      Net::put_f32(body, rs.pos.y());
      Net::put_f32(body, rs.vel.x());
      Net::put_f32(body, rs.vel.y());
      Net::put_u8(body, rs.flags);
      replay_->record_effect(Replay::FX_BULLET, (uint8_t)idx, body);
    }
  }
  Ship::replay_lance_flashes.clear();
  Ship::replay_shock_flashes.clear();
  Ship::replay_rings.clear();
  Ship::replay_pews.clear();
  Ship::replay_beam_pews.clear();
  Ship::replay_shots.clear();
}

// Finalize (header patch) and drop the recorder. ended=true (game over)
// also rotates current -> recent with the best check; ended=false (abandon
// to the menu / teardown) leaves current.nrp resumable.
void GLGame::replay_finish(bool ended) {
  if (!replay_) return;
  if (ended) {
    // The game-over latch runs BEFORE this tick's cadence record, so
    // without a final record the file ends up to ~100 ms short of the
    // actual ending — the last death never landed and playback froze one
    // breath before the GAME OVER card. One forced keyframe carries the
    // ended world (dead ships, zero lives) so playback reaches it.
    Save::MemStream payload;
    net_build_keyframe_payload(payload);
    replay_->record_keyframe(payload.data());
  }
  // The stamped score is what the leaderboard charts (LEADERBOARD.md).
  // ONLINE each side stamps its OWN pilot's score: co-op entries are
  // personal claims — one row per account, the partner's recording
  // carries theirs (decided 2026-08-02, replacing the shared max that
  // made the second submission a duplicate). OFFLINE keeps the best
  // ship's score — local 2P is one account claiming the run.
  uint32_t score = 0;
  if (net_mode_ == NetHost || net_mode_ == NetClient) {
    if (GLShip *me = local_player()) {
      int s = me->ship->score;
      if (s > 0) score = (uint32_t)s;
    }
  } else {
    for (auto *gs : *players) {
      int s = gs->ship->score;
      if (s > 0 && (uint32_t)s > score) score = (uint32_t)s;
    }
  }
  replay_->finalize(score, (uint32_t)(generation < 0 ? 0 : generation),
                    Achievements::unlocks_suppressed(), ended,
                    (uint8_t)players->size());
  delete replay_;
  replay_ = nullptr;
}

// ---- leaderboard game-over flow (LEADERBOARD.md L2) ----

// Called right after replay_finish(true) at every game-over latch. The
// trigger is the two-stage gate the plan locks: locally, the run just
// promoted best.nrp (take_best_promoted — clean, non-cheated, a new
// personal best, all enforced by the promotion itself); remotely, the
// qualify answer must say the score would place. Everything is async and
// abandonable — the GAME OVER card never waits on the network.
void GLGame::board_maybe_start() {
  if (net_mode_ == NetReplay) return;
  // Consume the one-shot promotion flag unconditionally (so it can't leak
  // into a later game over), THEN decide whether to prompt: only on a
  // build that can actually pass the worker's attestation requirement —
  // otherwise the upload is doomed to "unverified" (LEADERBOARD.md). The
  // promotion says WHICH best slot the run landed in (solo/co-op — best is
  // per-board), and that slot is what gets qualified and uploaded.
  board_up_path_ = Replay::take_best_promoted();
  if (board_up_path_.empty()) return;
  // leaderboard_prompts no longer gates the flow — it chooses how the
  // would-place answer is handled (ASK shows the prompt, AUTO uploads
  // straight away; see board_tick).
  if (!net_board_can_submit()) return;
  Replay::Header h;
  if (!Replay::read_header(board_up_path_, h)) return;
  board_ = NetBoard::create();
  if (!board_) return;
  board_up_retried_ = false;
  board_up_retry_deadline_ = 0;
  board_up_sent_cred_.clear();
  // Warm the async platform credential mint (Steam's ticket takes a
  // round-trip) so it is ready by the time the player answers YES.
  (void)net_board_verify_credential();
  board_->connect(net_board_url());
  std::string season(h.game_version,
                     strnlen(h.game_version, sizeof(h.game_version)));
  // The board keeps one co-op slot: every run with >= 2 players competes
  // on the players=2 board (FOURPLAYER.md D10), so send the slot, not the
  // raw count — the replay HEADER keeps the true count. The worker
  // normalizes too; sending the slot keeps the echoed `players` matching.
  const int board_players = std::min((int)h.player_count, 2);
  board_->qualify(season, board_players, h.final_score);
  board_score_ = h.final_score;
  board_q_season_ = season;
  board_q_players_ = board_players;
  board_q_retried_ = false;
  board_phase_ = BoardQualifying;
  board_deadline_ = current_time + BOARD_QUALIFY_TIMEOUT_MS;
  SDL_Log("board: qualify season=%s players=%u score=%u", season.c_str(),
          (unsigned)board_players, (unsigned)h.final_score);
}

void GLGame::board_tick() {
  if (!board_) return;
  // Waiting for a fresh credential after an "unverified" upload (see the
  // Error handler): poll peek (no re-mint) until a value different from the
  // rejected one appears, then consume + resubmit it once; give up at the
  // deadline.
  if (board_up_retry_deadline_) {
    std::string peek = net_board_verify_credential_peek();
    if (!peek.empty() && peek != board_up_sent_cred_) {
      board_up_retry_deadline_ = 0;
      const NetIdentity &me = net_local_identity();
      std::string fresh = net_board_verify_credential();  // consume the fresh one
      board_up_sent_cred_ = fresh;
      board_->submit(board_up_path_, me.platform, me.name, fresh);
      board_phase_ = BoardUploading;
      SDL_Log("board: retrying upload with a fresh credential");
    } else if (current_time >= board_up_retry_deadline_) {
      board_up_retry_deadline_ = 0;
      board_phase_ = BoardFailed;
      board_fail_reason_ = "unverified";
      SDL_Log("board: no fresh credential before deadline - upload failed");
      delete board_;
      board_ = nullptr;
      return;
    }
  }
  NetBoard::Event ev;
  while (board_->poll(ev)) {
    if (ev.kind == NetBoard::Event::Qualify &&
        board_phase_ == BoardQualifying) {
      if (ev.would_place) {
        if (!g_prefs.leaderboard_prompts) {
          // LEADERBOARD UPLOAD = AUTO: skip the question and upload the
          // best straight away — the card shows the same UPLOADING /
          // UPLOADED - RANK #N status text the prompted path shows
          // (decided with Glenn 2026-08-03: the setting picks ask-vs-auto,
          // it never means "don't upload"; declining per run is what ASK
          // is for).
          const NetIdentity &me = net_local_identity();
          std::string cred = net_board_verify_credential();
          board_up_sent_cred_ = cred;  // for the retry's freshness compare
          board_->submit(board_up_path_, me.platform, me.name, cred);
          board_phase_ = BoardUploading;
          SDL_Log("board: would place #%d - auto-uploading (prompt off)",
                  ev.place);
          continue;
        }
        board_phase_ = BoardPrompt;
        board_place_ = ev.place;
        board_yes_ = true;  // YES default (LEADERBOARD.md decision)
        board_prompt_shown_ = current_time;  // arms its own input grace
        board_prompt_pressed_.clear();  // only keys pressed FROM NOW act
        SDL_Log("board: would place #%d - prompting", ev.place);
      } else {
        SDL_Log("board: below the cut-line (place %d) - no prompt",
                ev.place);
        board_phase_ = BoardOff;
        delete board_;
        board_ = nullptr;
        return;
      }
    } else if (ev.kind == NetBoard::Event::Placed) {
      board_phase_ = BoardPlaced;
      board_place_ = ev.place;
      SDL_Log("board: placed #%d", ev.place);
      delete board_;
      board_ = nullptr;
      return;
    } else if (ev.kind == NetBoard::Event::Error) {
      SDL_Log("board: error %s", net_board_sanitize(ev.reason).c_str());
      // An "unverified" upload is usually a credential that was empty, stale
      // or single-use-consumed at submit time. The submit's own read already
      // fired the next mint, so start polling for a fresh (different) one and
      // retry ONCE (the socket stays open; the worker's err does not close
      // it). Keep board_ alive; the poll runs from the top of board_tick.
      if (board_phase_ == BoardUploading && !board_up_retried_ &&
          ev.reason == "unverified") {
        board_up_retried_ = true;
        board_up_retry_deadline_ = current_time + BOARD_UPLOAD_RETRY_TIMEOUT_MS;
        SDL_Log("board: upload unverified - waiting for a fresh credential");
        continue;
      }
      // An error during the prompt/upload shows on the card; one during
      // the silent qualify just cancels the whole idea.
      if (board_phase_ == BoardUploading || board_phase_ == BoardPrompt) {
        board_phase_ = BoardFailed;
        board_fail_reason_ = net_board_sanitize(ev.reason, 32);
      } else {
        board_phase_ = BoardOff;
      }
      delete board_;
      board_ = nullptr;
      return;
    } else if (ev.kind == NetBoard::Event::Closed) {
      // A drop while still qualifying gets ONE silent reconnect (a phone's
      // first connection after a radio wake routinely fails); the qualify
      // frame re-queues until the new socket opens, and the original
      // deadline still bounds the whole affair.
      if (board_phase_ == BoardQualifying && !board_q_retried_ &&
          current_time < board_deadline_) {
        board_q_retried_ = true;
        delete board_;
        board_ = NetBoard::create();
        if (board_) {
          SDL_Log("board: connection lost while qualifying - retrying");
          board_->connect(net_board_url());
          board_->qualify(board_q_season_, board_q_players_, board_score_);
          return;  // fresh socket; poll it next tick
        }
        board_phase_ = BoardOff;
        return;
      }
      SDL_Log("board: connection closed");
      if (board_phase_ == BoardUploading) {
        board_phase_ = BoardFailed;
        board_fail_reason_ = "connection";
      } else if (board_phase_ != BoardPlaced &&
                 board_phase_ != BoardFailed) {
        board_phase_ = BoardOff;
      }
      delete board_;
      board_ = nullptr;
      return;
    }
  }
  if (board_phase_ == BoardQualifying && current_time > board_deadline_) {
    SDL_Log("board: qualify timed out - no prompt");
    board_phase_ = BoardOff;
    delete board_;
    board_ = nullptr;
  }
}

bool GLGame::board_nav(char key) {
  if (board_phase_ == BoardPrompt) {
    // The prompt can appear AFTER the card's 3 s game-over grace (the
    // qualify deadline runs 15 s), so anchor the accidental-input guard on
    // when the PROMPT appeared, not on game over: a keypress already in
    // flight to leave must not answer the just-shown YES-default prompt.
    // Swallowed, not passed on — the exit paths behind us are equally
    // graced, so nothing is lost.
    if (current_time - board_prompt_shown_ < BOARD_PROMPT_ARM_MS)
      return true;
    unsigned char k = (unsigned char)key;
    if (MenuSelect::is_up(k) || MenuSelect::is_down(k)) {
      board_yes_ = !board_yes_;
      return true;
    }
    if (MenuSelect::is_back(k)) {
      SDL_Log("board: prompt declined");
      board_phase_ = BoardOff;
      delete board_;
      board_ = nullptr;
      return true;
    }
    if (MenuSelect::is_confirm(k)) {
      if (!board_yes_) {
        SDL_Log("board: prompt declined");
        board_phase_ = BoardOff;
        delete board_;
        board_ = nullptr;
        return true;
      }
      const NetIdentity &me = net_local_identity();
      std::string cred = net_board_verify_credential();
      board_up_sent_cred_ = cred;  // for the retry's freshness compare
      board_->submit(board_up_path_, me.platform, me.name, cred);
      board_phase_ = BoardUploading;
      SDL_Log("board: uploading %s",
              board_up_path_ == Replay::best_coop_path() ? "best_coop.nrp"
                                                         : "best.nrp");
      return true;
    }
    return true;  // the prompt owns the card: swallow everything else
  }
  if (board_phase_ == BoardUploading) {
    // Esc cancels the upload (back to the plain card); anything else is
    // swallowed so a stray confirm can't leave mid-transfer. Leaving via
    // the menu key still works — the destructor abandons the transfer.
    if (MenuSelect::is_back((unsigned char)key)) {
      SDL_Log("board: upload cancelled");
      board_phase_ = BoardOff;
      delete board_;
      board_ = nullptr;
    }
    return true;
  }
  return false;  // Placed/Failed/Off: the normal exit handling runs
}

// ---- replay playback (REPLAY.md R2) ----

// Playback ctor. Delegates to the save-restore ctor (the same world rebuild
// a joining net client's bootstrap uses), then re-badges the game as a
// replay: every ship is a ghost — no input reaches them (keyboard/controller
// handlers gate on NetReplay), and is_local_player is stripped so no
// kill-credit or stats path can earn from watching. The delegated ctor ran
// the new-game/cheat achievement hooks as if a real game were starting;
// that's inert here (ghosts can't earn, and the next real game re-runs the
// hooks) but noted for honesty.
GLGame::GLGame(const Save::GameState &snapshot, Replay::Reader *reader)
  : GLGame(snapshot, (SDL_GameController *)NULL) {
  net_mode_ = NetReplay;
  // Quiet restores, exactly like the net client: every 10 Hz state apply
  // runs restore_state -> respawn -> reset(), and an un-quiet reset()
  // CLEARS the presentation vectors — the received lance pulses, shock
  // bolts and shockwave rings the REC_EFFECT records just pushed died
  // within 100 ms of appearing (sound played, flash never drew — Glenn),
  // and impact debris froze the same way the client comment describes.
  Ship::net_quiet_respawn = true;
  // The delegated ctor reported this world as rich presence ("LEVEL 14
  // CO-OP"), so watching a replay told your friends you were playing it —
  // and a 2-player replay claimed a co-op game on a machine sitting in a
  // menu. Presence is descriptive; describe watching.
  Presence::set_menu();
  replay_reader_ = reader;
  replay_save_version_ = reader->header().save_version
                             ? reader->header().save_version
                             : Save::GameState::VERSION;
  // A bootstrap keyframe recorded mid-time-slow seeds the effect via the
  // delegated restore — kept: the client-path mirror (net_apply_state
  // adopt + the extrapolation loop's paced countdown) plays it back
  // exactly like a live client, against record spacing that already
  // carries the slow motion in wall time.
  for (auto *gs : *players) {
    gs->ship->is_local_player = false;
    // The delegated restore resurrects a mid-countdown ship instantly
    // (restore_state -> respawn(was_killed) — deliberate for RESUMING a
    // save: you come back alive without re-waiting the countdown), and
    // respawn's detonate() mints its flash as real bullets. For playback
    // that's an explosion before a new game's first countdown; scrub the
    // debris — the bootstrap extras then silently re-kill the ship and
    // the countdown plays out exactly as recorded.
    gs->ship->bullets.clear();
  }
}

GLGame *GLGame::start_replay_playback(const std::string &path) {
  // A MISSING file is normal life, not damage: a game over ROTATES
  // current -> recent (R1 lifecycle), so asking for `current` right after
  // finishing a run correctly finds nothing. Say that, and point at where
  // the run went, instead of the scary decline below.
  FILE *probe = path.empty() ? NULL : fopen(path.c_str(), "rb");
  if (!probe) {
    SDL_Log("replay: nothing to play at %s", path.c_str());
    Replay::Header h;
    if (path == Replay::current_path() &&
        Replay::read_header(Replay::recent_path(), h))
      SDL_Log("replay: no active run - the last completed run (score=%u) "
              "is in recent.nrp (NEWTONIA_REPLAY_PLAY=recent)",
              h.final_score);
    return NULL;
  }
  fclose(probe);

  Replay::Reader *r = new Replay::Reader(path);
  Replay::Reader::Record rec;
  if (!r->ok() || !r->next(rec) || rec.kind != Replay::REC_KEYFRAME) {
    // Unreadable, out-of-range format version, or no leading keyframe:
    // decline politely (REPLAY.md — "REPLAY FROM AN OLDER VERSION" is the
    // R3 menu's rendering of this NULL).
    SDL_Log("replay: cannot play %s (%s)", path.c_str(),
            r->ok() ? "no leading keyframe (empty recording?)"
                    : "unreadable or older format version");
    delete r;
    return NULL;
  }
  uint16_t sv = r->header().save_version ? r->header().save_version
                                         : Save::GameState::VERSION;
  std::vector<uint8_t> buf(rec.payload, rec.payload + rec.len);
  Save::MemStream in(buf);
  Save::GameState s;
  if (!Save::deserialize_game(in, s, sv) || !net_state_sane(s)) {
    SDL_Log("replay: bootstrap keyframe unparseable - declining");
    delete r;
    return NULL;
  }
  GLGame *g = new GLGame(s, r);
  // The extras behind the game state — ship poses/bullets, mini-station and
  // enemy bullets, asteroid id adoption — exactly like the lobby bootstrap.
  // Bootstrap-silent: state transitions in this first apply are initial
  // conditions, not events (no death explosion for a run that starts in
  // the spawn countdown).
  g->replay_bootstrap_apply_ = true;
  g->net_apply_extras(in, s, sv);
  g->replay_bootstrap_apply_ = false;
  // The timeline starts at the bootstrap record's slot (0 for a fresh run).
  // Reader::next only yields slots inside MAX_RECORD_SLOT, chosen so that
  // slot * 100 stays inside an int — the guard has to be on the READ side,
  // because the overflow would happen here in the multiply.
  g->replay_clock_ms_ = (int)rec.slot * 100;
  SDL_Log("replay: playback started (%s, %d slots, %u player%s)",
          path.c_str(), r->last_slot() + 1,
          (unsigned)r->header().player_count,
          r->header().player_count == 1 ? "" : "s");
  return g;
}

// Apply every record that has come due on the playback clock — the file-fed
// stand-in for net_client_poll. Records arrive in file order (the reliable-
// ordered channel of the replay world), so none of the client's stale/
// reorder gates are needed.
void GLGame::tick_replay_poll(int delta) {
  if (replay_finished_ || !replay_reader_) return;
  replay_clock_ms_ += delta;
  net_event_effect_budget_ = NET_EVENT_EFFECTS_PER_POLL;
  // Cap the catch-up. Unlike the live client — which applies records as the
  // network hands them over — playback applies everything the CLOCK says is
  // due, and the clock is the raw frame delta (nothing upstream clamps it)
  // multiplied by up to 4x for fast-forward. So one stalled frame turns
  // into a burst of full deserialize + world-rebuild applies inside the
  // next frame, which lengthens it, which enlarges the next burst. Bound
  // the burst and leave the clock alone: the backlog drains over the
  // following frames (8 per frame at 60 fps is 480 records/s, so even a
  // 12-second stall is caught up in a quarter of a second) instead of
  // landing in one hitch.
  int budget = 8;
  for (;;) {
    int slot = replay_reader_->peek_slot();
    if (slot < 0) {
      replay_finished_ = true;
      // Past the last record the world freezes and no further state applies
      // — so every continuous loop the records were driving (the shield hum
      // from a ghost's invincibility window, god-mode music) would be left
      // playing forever on the REPLAY ENDED screen. Same shape as the
      // client's lost-host silencing: when the state source stops, nothing
      // is left to turn them off, so turn them off here. The engine loop is
      // muted by the freeze block in tick_net_client.
      for (auto *gs : *players) gs->ship->silence_loops();
      SDL_Log("replay: playback finished (slot %d)",
              replay_reader_->last_slot());
      break;
    }
    // 64-bit: slot is a u32 off the file, and `slot * 100` as int overflows
    // above ~21M — reachable only by a corrupt or hostile file today, but
    // R4 downloads replay blobs from a leaderboard, and this is the
    // arithmetic that decides when to apply them.
    if ((int64_t)slot * 100 > (int64_t)replay_clock_ms_) break;
    if (budget-- <= 0) break;
    Replay::Reader::Record rec;
    replay_reader_->next(rec);
    if (rec.kind == Replay::REC_EFFECT) {
      // Transient weapon visuals: feed the exact net receive paths (lance/
      // shock flashes + sounds) or mint the recorded shockwave ring on the
      // owning ghost. Display-only — kills arrive in the state records.
      if (rec.len < 2) continue;
      uint8_t subtype = rec.payload[0], idx = rec.payload[1];
      Ship *fx_ship = NULL;
      uint8_t i = 0;
      for (auto *gs : *players) {
        if (i++ == idx) { fx_ship = gs->ship; break; }
      }
      if (!fx_ship) continue;
      if (subtype == Replay::FX_LANCE || subtype == Replay::FX_SHOCK) {
        Net::Reader r(rec.payload + 2, rec.len - 2);
        if (subtype == Replay::FX_LANCE)
          net_receive_lance_pulse(r, fx_ship);
        else
          net_receive_shock_pulse(r, fx_ship, NULL);
      } else if (subtype == Replay::FX_SHOT && rec.len >= 2 + 8) {
        // Gun-shot cue: attenuate against the playback camera exactly like
        // EV_WORLD_SHOT (chunks are per-instance; every play site sets
        // volume first, so borrowing a ship's is safe). The trailing kind
        // byte picks the chunk — 1 = beam (the piercing-clone sound rule).
        float sx, sy;
        memcpy(&sx, rec.payload + 2, 4);
        memcpy(&sy, rec.payload + 6, 4);
        uint8_t snd_kind = rec.len >= 2 + 9 ? rec.payload[10] : 0;
        if (std::isfinite(sx) && std::isfinite(sy)) {
          // One sound per burst, mirroring net_spawn_reported_bullet's
          // 40 ms window: the host's online tee records one FX_SHOT per
          // MSG_SHOT — per BULLET — so a spread gun's pull is N records
          // in the same slot, and playing each stacked N identical
          // samples into one very loud bang (the artifact the live
          // window exists to prevent). Replay-clock domain, so
          // fast-forward doesn't over-suppress.
          if (idx < REPLAY_FX_SHOT_PLAYERS &&
              replay_clock_ms_ - replay_fx_shot_ms_[idx] < 40)
            continue;
          if (idx < REPLAY_FX_SHOT_PLAYERS)
            replay_fx_shot_ms_[idx] = replay_clock_ms_;
          // Nearest-ghost attenuation — the same offline rule the recorded
          // game played by (net_listener_volume would ignore P2's viewport).
          float vol = sound_volume_for_point(Point(sx, sy));
          Mix_Chunk *snd = fx_ship->shoot_sound;
          // Cached, not a lazy Mix_LoadWAV: decoding a WAV costs ~50 ms
          // (the cost sound_cache.h exists to pay once), and a lazy load
          // spends it inside the frame that plays the first beam shot of a
          // playback.
          //
          // Held in a static, like the lance/shock chunks below: what
          // load_wav_cached caches is the decoded SAMPLE BUFFER — it hands
          // back a fresh Mix_QuickLoad_RAW wrapper every call, and every
          // other caller in the codebase stores that wrapper in a member its
          // destructor frees. Calling it per record leaked one chunk header
          // per beam shot for the length of the playback.
          if (snd_kind == 1) {
            static Mix_Chunk *beam_snd = load_wav_cached("audio/beam.wav");
            if (beam_snd) snd = beam_snd;
          }
          if (vol > 0.0f && snd != NULL) {
            Mix_VolumeChunk(snd, (int)(MIX_MAX_VOLUME * (vol > 1.0f ? 1.0f : vol)));
            Mix_PlayChannel(-1, snd, 0);
          }
        }
      } else if (subtype == Replay::FX_BULLET && rec.len >= 2 + 17) {
        // v2: spawn the exact bullet at its muzzle, the moment it was
        // fired, instead of letting it first appear in the next 10 Hz
        // snapshot already down-range and off the nose. Quiet — the
        // FX_SHOT cue above is this shot's sound. Ordering contract with
        // the recorder: effects are stamped with the slot of the state
        // record they FOLLOW (record_effect), so this clone spawns after
        // that record's wholesale rebuild has run and lives until the
        // NEXT slot's rebuild replaces it with authority's copy — the
        // online client's MSG_SHOT visual. (Stamped one slot later they
        // preceded their own slot's rebuild in the same poll batch, and
        // every clone was destroyed before a single draw.)
        float bx, by, bvx, bvy;
        memcpy(&bx, rec.payload + 2, 4);
        memcpy(&by, rec.payload + 6, 4);
        memcpy(&bvx, rec.payload + 10, 4);
        memcpy(&bvy, rec.payload + 14, 4);
        uint8_t bflags = rec.payload[18];
        if (std::isfinite(bx) && std::isfinite(by) && std::isfinite(bvx) &&
            std::isfinite(bvy))
          fx_ship->net_spawn_reported_bullet(
              0, Point(bx, by), Point(bvx, bvy), (bflags & 1) != 0,
              (bflags & 2) != 0, (bflags & 4) != 0, /*quiet=*/true);
      } else if (subtype == Replay::FX_RING && rec.len >= 2 + 21) {
        float x, y, max_r, speed, duration;
        memcpy(&x, rec.payload + 2, 4);
        memcpy(&y, rec.payload + 6, 4);
        memcpy(&max_r, rec.payload + 10, 4);
        memcpy(&speed, rec.payload + 14, 4);
        memcpy(&duration, rec.payload + 18, 4);
        bool nova = rec.payload[22] != 0;
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(max_r) &&
            std::isfinite(speed) && std::isfinite(duration) &&
            max_r > 0.0f && max_r < 100000.0f && duration > 0.0f &&
            duration < 600000.0f) {
          fx_ship->shockwaves.push_back(
              Shockwave(Point(x, y), max_r, speed, duration, nova));
          // Both ring types play the giga blast at the mint site.
          if (fx_ship->giga_mine_explode_sound != NULL)
            Mix_PlayChannel(-1, fx_ship->giga_mine_explode_sound, 0);
          NET_LOG("net: replay ring (%s)\n", nova ? "nova" : "giga");
        }
      }
      continue;
    }
    if (rec.kind == Replay::REC_EVENTS) {
      if (rec.len >= 5) {
        uint8_t code = rec.payload[0];
        uint32_t arg = 0;
        memcpy(&arg, rec.payload + 1, 4);
        // Never replayed: session control (pause emits no records by
        // design; BYE is transport lifecycle), EV_ACHIEVEMENT (watching
        // must never poke a platform SDK) and EV_RAM_BLAST (mints real
        // bullets + kill claims on a "local ship" a replay doesn't have).
        if (code != Net::EV_PAUSE && code != Net::EV_RESUME &&
            code != Net::EV_BYE && code != Net::EV_ACHIEVEMENT &&
            code != Net::EV_RAM_BLAST)
          net_handle_event(code, arg);
      }
      continue;
    }
    // Only the two state kinds reach the deserializer. An unknown kind used
    // to fall through to it and be parsed as a delta — harmless today (the
    // header's version gate turns away files from a build that could have
    // written one) but exactly the kind of implicit default that stops being
    // harmless the moment a kind is added.
    if (rec.kind != Replay::REC_KEYFRAME && rec.kind != Replay::REC_DELTA)
      continue;
    std::vector<uint8_t> buf(rec.payload, rec.payload + rec.len);
    Save::MemStream in(buf);
    Save::GameState s;
    if (!Save::deserialize_game(in, s, replay_save_version_)) continue;
    if (!net_state_sane(s)) continue;
    net_apply_state(s);
    if (rec.kind == Replay::REC_KEYFRAME) {
      net_apply_extras(in, s, replay_save_version_);
    } else {
      if (!net_apply_ship_extras(in, s, true, replay_save_version_)) continue;
      net_apply_delta_asteroids(in);
    }
  }
}

// Host-side toggle (G key, or the touch region on the HUD text). The
// flip persists as the host's own preference and — online — is announced
// as the room rule. The client never reaches this (host_keys / the
// net_mode_ guard in touch_tap).
void GLGame::host_toggle_friendly_fire() {
  friendly_fire = !friendly_fire;
  g_prefs.friendly_fire = friendly_fire;
  save_preferences();
  for (auto *p : *players) p->ship->missiles_seek_players = friendly_fire;
  net_send_event(Net::EV_FRIENDLY_FIRE, friendly_fire ? 1u : 0u);
}

// The host event outboxes are process-wide statics holding raw Ship*/
// positions queued during a tick and drained at its end. They must not
// carry entries across GLGame instances: a client game populates them
// (its local ship pushes to net_shots on every shot), and if that game is
// deleted before the entries are drained, a later host game's drain would
// dereference freed ships. Clearing at construction guarantees each game
// starts from empty regardless of how the previous one ended.
void GLGame::net_clear_event_outboxes() {
  Ship::net_ship_impacts.clear();
  Ship::net_shots.clear();
  Ship::net_booms.clear();
  Ship::net_kill_claims.clear();
  Ship::net_shot_reports.clear();
  Ship::net_ship_hit_claims.clear();
  Ship::net_lance_reports.clear();
  Ship::net_shock_reports.clear();
  Ship::net_bounce_reports.clear();
  Ship::net_ach_relays.clear();
  Ship::net_ram_blasts.clear();
  Ship::replay_lance_flashes.clear();
  Ship::replay_shock_flashes.clear();
  Ship::replay_rings.clear();
  Ship::replay_pews.clear();
  Ship::replay_beam_pews.clear();
  Ship::replay_shots.clear();
}

void GLGame::net_send_event(uint8_t code, uint32_t arg) {
  // Replay tee (REPLAY.md R1): solo games have no session, so without this
  // the EV_* cues would vanish — there is no offline outbox. Session-control
  // events are skipped (pause emits no records by design; BYE is transport
  // lifecycle; ACHIEVEMENT must never re-poke a platform SDK on playback).
  // EV_WORLD_SHOT is skipped too: its recorded twin is the FX_SHOT record
  // from the Ship::replay_pews outbox — recording both played the
  // mini-station's every shot twice in a host-side online replay. (The
  // client's file still gets it: its tee sits at the RECEIVE site, and the
  // wire send below is untouched.)
  if (replay_ && code != Net::EV_PAUSE && code != Net::EV_RESUME &&
      code != Net::EV_BYE && code != Net::EV_ACHIEVEMENT &&
      code != Net::EV_WORLD_SHOT)
    replay_->record_event(code, arg);
  // While the joiner is disconnected (rejoinable loss) the host plays on
  // with no session at all — level completion and pause still fire events.
  // B4: one encode, broadcast to every peer WITH a session — not gated on
  // `lost` (matching the old single-session guard: a dead transport eats
  // the bytes, and the rejoin resync ordering relies on sending the
  // moment a fresh session exists).
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_EVENT, (uint8_t)net_local_seat());
  Net::put_u8(msg, code);
  Net::put_u32(msg, arg);
  for (NetPeer *p : net_peers_)
    if (p->session)
      p->session->transport()->send_reliable(&msg[0], msg.size());
}

// B4 (PB-D4): the targeted form — session-scoped state that belongs to ONE
// peer (its pickup latch, its achievement relay, its first-INPUT resync)
// must not re-sync every other client. Same replay tee and skip list as
// the broadcast form — but the tee is per EVENT, not per peer: a fan-out
// that sends one collection to N peers tees on the first call and passes
// tee=false on the rest, or the replay would hold N records per pickup.
void GLGame::net_send_event_to(NetPeer &peer, uint8_t code, uint32_t arg,
                               bool tee) {
  if (tee && replay_ && code != Net::EV_PAUSE && code != Net::EV_RESUME &&
      code != Net::EV_BYE && code != Net::EV_ACHIEVEMENT &&
      code != Net::EV_WORLD_SHOT)
    replay_->record_event(code, arg);
  if (!peer.session) return;
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_EVENT, (uint8_t)net_local_seat());
  Net::put_u8(msg, code);
  Net::put_u32(msg, arg);
  peer.session->transport()->send_reliable(&msg[0], msg.size());
}

// Seat-identity relay (MSG_PEER_IDENT, net_protocol.h): every remote
// seat's folded badge identity to one client, one message per seat. The
// receiver's own seat rides along (it ignores it) so one loop serves any
// target. Legacy/unknown identities are sent as-is — all-zero fields are
// exactly the no-badge state the receiver's store defaults to.
void GLGame::net_send_seat_identities_to(NetPeer &peer) {
  if (net_mode_ != NetHost || !peer.session) return;
  for (NetPeer *src : net_peers_) {
    if (src->seat < 2 || (int)src->seat > MAX_PLAYERS) continue;
    std::string name = src->identity.name;
    if ((int)name.size() > NET_IDENTITY_NAME_MAX)
      name.resize(NET_IDENTITY_NAME_MAX);
    std::vector<uint8_t> msg;
    Net::put_header(msg, Net::MSG_PEER_IDENT, (uint8_t)net_local_seat());
    Net::put_u8(msg, src->seat);
    Net::put_u8(msg, src->identity.platform);
    Net::put_u8(msg, src->identity.platform_trust);
    Net::put_u8(msg, src->identity.name_trust);
    Net::put_u8(msg, (uint8_t)name.size());
    if (!name.empty())
      Net::put_bytes(msg, (const uint8_t *)name.data(), name.size());
    // Flags TRAILING, savegame-style: this message already shipped at
    // PROTO 25 without it, so a mid-message insert would misparse against
    // those builds (old reader takes flags for name_len). Appended, an old
    // reader stops short and a missing byte reads as 0 — no carve-out,
    // the under-render direction.
    Net::put_u8(msg, src->offline_paired ? 1 : 0);  // flags: bit0 LAN-paired
    peer.session->transport()->send_reliable(&msg[0], msg.size());
  }
}

// Roster-wide re-send: cheap (a few bytes per seat) and idempotent on the
// receiver, so identity changes just re-broadcast rather than tracking
// per-client deltas. Lost peers are skipped like net_send_event's guard
// isn't — a dead transport eats bytes harmlessly, but a LOST peer's fresh
// rejoin session must not see a stale roster mid-handshake, and its own
// first INPUT re-sends everything anyway.
void GLGame::net_broadcast_seat_identities() {
  if (net_mode_ != NetHost) return;
  for (NetPeer *p : net_peers_)
    if (p->session && !p->lost) net_send_seat_identities_to(*p);
}

void GLGame::net_handle_event(uint8_t code, uint32_t arg, NetPeer *from) {
  switch (code) {
    case Net::EV_PAUSE:
      if (running) toggle_pause(false);
      break;
    case Net::EV_RESUME:
      if (!running) toggle_pause(false);
      break;
    case Net::EV_GENERATION_START:
      // The world rebuild itself rides the next snapshot (client side);
      // the event just drives the banner.
      net_set_generation_banner((int)arg);
      break;
    case Net::EV_BYE:
      // B5: the goodbye marks the SENDING peer lost — on the host that is
      // the peer whose transport delivered it, not the front slot.
      (from ? *from : net_peer_make()).lost = true;
      // A BYE is a DELIBERATE goodbye, not a dropped link. The client must
      // not auto-rejoin a room whose host said it isn't coming back (the
      // relay may still hold it in reclaim grace if the host's signal
      // socket was already dead — joins would be admitted and wait
      // forever). Clearing the code disables the rejoin path and flips the
      // card to its exit variant; the lobby's clipboard auto-join refuses
      // the dead code too.
      if (net_mode_ == NetClient && !net_room_code_.empty()) {
        net_peer_bye_ = true;
        NetLobby::mark_room_dead(net_room_code_);
        net_room_code_.clear();
        NET_LOG("net: host bye - no rejoin\n");
      }
      // Same for a LAN-door session: a deliberate goodbye must not send
      // the client browsing for a host that isn't coming back.
      if (net_mode_ == NetClient && !net_lan_host_name_.empty()) {
        net_peer_bye_ = true;
        net_lan_host_name_.clear();
        NET_LOG("net: host bye - no lan rejoin\n");
      }
      break;
    case Net::EV_KICKED:
      // The host removed us. Terminal by exactly the BYE machinery — the
      // room code is dead to us, the rejoin path must not fire (a client
      // that reconnected on its own would undo the kick), and the card
      // takes its own wording off net_kicked_.
      if (net_mode_ == NetClient) {
        net_peer_make().lost = true;
        net_kicked_ = true;
        net_peer_bye_ = true;
        if (!net_room_code_.empty()) {
          NetLobby::mark_room_dead(net_room_code_);
          net_room_code_.clear();
        }
        net_lan_host_name_.clear();
        NET_LOG("net: kicked by the host - no rejoin\n");
      }
      break;
    case Net::EV_ACHIEVEMENT: {
      // The host's sim detected an unlock it attributes to OUR ship (ram
      // kills and station kills only resolve host-side). Our own cheat
      // suppression still applies inside unlock(). Effect-budgeted: EVENT is
      // never drop-gated by the polls (reliable control must flow), so the
      // codes that do real work bound themselves instead — a flood of these
      // would otherwise hammer the platform achievements SDK.
      if (net_event_effect_budget_ <= 0) break;
      net_event_effect_budget_--;
      if (arg == Net::ACH_SHIELD_RAM) Achievements::unlock("shield_ram");
      else if (arg == Net::ACH_SHIELD_RAM_ASTEROID) Achievements::unlock("shield_ram_asteroid");
      else if (arg == Net::ACH_MINI_STATION_KILL) Achievements::unlock("mini_station_kill");
      else if (arg == Net::ACH_STATION_DESTROYED) Achievements::unlock("station_destroyed");
      break;
    }
    case Net::EV_RAM_BLAST: {
      // Our shielded ram killed an asteroid host-side and burst bullets
      // out of our replica — our own bullet echo is skipped, so mint the
      // blast here. net_blast on the local ship (net_claim_kills) goes
      // into REAL bullets: instant local kills + bullet_id-0 claims,
      // exactly like an own-mine explosion. Our pose is authoritative and
      // the host adopted it, so blasting at our current position lands
      // where the player just saw the impact. Effect-budgeted (see
      // EV_ACHIEVEMENT): each blast spawns ~10 bullets, and EVENT itself is
      // never drop-gated, so the spawn bounds itself per poll.
      if (net_event_effect_budget_ <= 0) break;
      net_event_effect_budget_--;
      if (!players->empty()) {
        Ship *me = players->back()->ship;
        if (me->is_alive())
          me->net_blast(me->position, me->velocity, 10);
      }
      break;
    }
    case Net::EV_FRIENDLY_FIRE: {
      // The host's preference is the room rule; adopt it for the HUD only
      // (damage runs in the host sim). g_prefs stays the player's own.
      bool on = arg != 0;
      if (on != friendly_fire && net_ff_synced_) {
        net_banner_text_ = on ? "FRIENDLY FIRE ON" : "FRIENDLY FIRE OFF";
        net_banner_ms_ = 2000;
        net_banner_header_ = false;
      }
      net_ff_synced_ = true;
      friendly_fire = on;
      // Keep the local missile sim honest too: seeking is cosmetic-ish on
      // the client but a missile visibly hunting the partner reads wrong.
      for (auto *p : *players) p->ship->missiles_seek_players = friendly_fire;
      NET_LOG("net: friendly fire %s\n", on ? "on" : "off");
      break;
    }
    case Net::EV_ROID_THUD:
    case Net::EV_ROID_TING: {
      // Since PROTO 10 only the gen-20 station-hull deflection sends
      // THUD (asteroid impacts are client-side cosmetic — see
      // Ship::net_cosmetic_impacts); TING is retired but kept decodable.
      // Play the cue and spark any asteroid near the packed position.
      Mix_Chunk *snd = code == Net::EV_ROID_TING ? Asteroid::ting_sound
                                                 : Asteroid::thud_sound;
      NET_LOG("net: roid impact\n");
      float ix, iy;
      Net::unpack_pos(arg, ix, iy, world.x(), world.y());
      WorldSound::play(snd, Point(ix, iy));
      net_spark_asteroid_at(ix, iy);
      break;
    }
    case Net::EV_LEVEL_TIC:
      if (tic_sound)
        Mix_PlayChannel(-1, tic_sound, 0);
      break;
    case Net::EV_PICKUP:
      if (pickup_sound)
        Mix_PlayChannel(-1, pickup_sound, 0);
      // A pickup was collected host-side; its ammo bump reaches us only via
      // the next snapshot. `arg` names WHICH of OUR limited primaries gained
      // ammo (NetPrimaryKind; 0 = a pickup that changes none of our primary
      // ammo — the host's own, or a non-primary type). Arm that weapon's
      // anti-flicker latch so net_apply_state accepts the coming INCREASE on
      // exactly that primary instead of clamping it away as a lag blip. The
      // latch was originally ONE shared scalar armed by ANY pickup — so a
      // lag blip on a different weapon could consume it (permanently pinning
      // the real pickup's ammo low) and an unrelated host-side pickup could
      // launder a blip through. Keyed per weapon, neither can happen.
      // Consumed on the accepted rise; self-expiring (~1.2 s).
      if (arg >= 1 && arg < NET_PRIMARY_KINDS)
        net_pickup_latch_[arg] = 12;
      break;
    case Net::EV_ROID_BOUNCE:
      // LEGACY decode only (old replay files, old hosts): the superseded
      // bounce event carried a playback VOLUME — the loudest listener's,
      // not ours — and no position, so flat chunk-volume play is all it
      // can support. New writers send EV_ROID_BOUNCE_AT below.
      if (Asteroid::asteroid_ting_sound) {
        Mix_VolumeChunk(Asteroid::asteroid_ting_sound,
                        (int)(arg & 0xff) * MIX_MAX_VOLUME / 255);
        Mix_PlayChannel(-1, Asteroid::asteroid_ting_sound, 0);
      }
      break;
    case Net::EV_ROID_BOUNCE_AT: {
      // Asteroid-vs-reflective bounce at a packed contact position, with
      // the host's impulse loudness. Re-attenuated against THIS machine's
      // camera like every other world cue — the legacy event above played
      // the host's own loudness flat, which put every bounce beside the
      // host at full volume in this cockpit.
      float ix, iy, ring_base;
      Net::unpack_pos_vol(arg, ix, iy, ring_base, world.x(), world.y());
      WorldSound::play(Asteroid::asteroid_ting_sound, Point(ix, iy),
                       ring_base);
      break;
    }
    // (EV_REMOTE_SHOT retired at PROTO 25 — nothing wrote it since the
    // PROTO 17 MSG_SHOT echo; a code 12 in an old replay's records falls
    // to the default case and is dropped silently.)
    case Net::EV_WORLD_SHOT:
    case Net::EV_WORLD_BOOM:
    case Net::EV_STATION_BOOM: {
      // Host-simulated world actors (enemies, mini-station): play the
      // cue attenuated by distance from our ship to the packed position.
      // Chunks are per-instance and every play site sets volume first,
      // so borrowing the local ship's is safe.
      float wx, wy;
      Net::unpack_pos(arg, wx, wy, world.x(), world.y());
      float vol = net_listener_volume(Point(wx, wy));
      if (vol <= 0.0f || players->empty()) break;
      if (code == Net::EV_STATION_BOOM) {
        // Rare and important enough to survive a saturated mixer.
        play_priority_chunk(station_explode_sound, vol);
        break;
      }
      Mix_Chunk *snd = code == Net::EV_WORLD_SHOT
                           ? local_player()->ship->shoot_sound
                           : local_player()->ship->explode_sound;
      if (snd) {
        Mix_VolumeChunk(snd, (int)(MIX_MAX_VOLUME * vol));
        Mix_PlayChannel(-1, snd, 0);
      }
      break;
    }
    case Net::EV_SHIP_IMPACT: {
      // Non-fatal ship-vs-asteroid bounce: debris spray on that ship
      // (explode() fills the object's own debris, which is never
      // serialized — hence invisible on the client until now).
      // PROTO 25: arg bits 0-7 are the seat (1..MAX_PLAYERS; was 1|2).
      int idx = (int)(arg & 0xff);
      GLShip *gs = player_by_seat(idx);
      if (gs && gs->ship->is_alive()) {
        gs->ship->explode(gs->ship->position, gs->ship->velocity * 0.25f);
        if (arg & 0x100)
          WorldSound::play(Asteroid::ting_sound, gs->ship->position);
      }
      break;
    }
    default:
      break;
  }
}

// Client: debris spray on the asteroid nearest an impact position (the
// impact events carry where, not which — ids would cost more bytes than
// a proximity match is worth at these rates).
void GLGame::net_spark_asteroid_at(float x, float y) {
  // Point-probe the grid (the client refreshes it each tick) rather than
  // scanning every asteroid — CLAUDE.md: use Grid for spatial queries. A
  // zero-radius probe with proximity 80 matches the old center-distance <
  // asteroid.radius + 80 test; the grid holds only asteroids, so the cast
  // is safe.
  Object probe(WrappedPoint(x, y), Point(0, 0));
  probe.radius = 0.0f;
  Object *hit = grid.collide(probe, 80.0f);
  if (hit) static_cast<Asteroid *>(hit)->explode();
}

void GLGame::net_set_generation_banner(int gen) {
  const char *name = NULL;
  switch (gen) {
    case 1:  name = "INVINCIBLE";    break;
    case 2:  name = "REFLECTIVE";    break;
    case 3:  name = "TELEPORTING";   break;
    case 4:  name = "INVISIBLE";     break;
    case 5:  name = "QUANTUM";       break;
    case 6:  name = "TOUGH";         break;
    case 7:  name = "ARMOURED";      break;
    case 8:  name = "PHASING";       break;
    case 9:  name = "BLACK HOLE";    break;
    case 10: name = "MINI STATION";  break;
    case 20: name = "ENEMY STATION"; break;
    default: break;
  }
  char buf[64];
  if (name)
    snprintf(buf, sizeof(buf), "LEVEL %d - %s", gen + 1, name);
  else
    snprintf(buf, sizeof(buf), "LEVEL %d", gen + 1);
  net_banner_text_ = buf;
  net_banner_ms_ = 2000;
  net_banner_header_ = false;
}


// ---- client side ---------------------------------------------------------

GLGame::GLGame(const Save::GameState &snapshot, NetSession *session,
               SDL_GameController *controller)
  : GLGame(snapshot, (SDL_GameController *)NULL) {
  net_mode_ = NetClient;
  Net::set_net_log_role(false);  // lobby set it too; belt & braces
  net_peer_make().session = session;
  net_peer_make().seat = (uint8_t)session->peer_seat();  // the host: seat 1
  net_peer_make().identity = session->peer_identity();
  // The joiner's complement of the host's "<NAME> JOINED" greeting: name
  // whose game this is ("JOINED GLENN SERVER"; the host is player 1, so a
  // nameless/legacy host reads "JOINED PLAYER 1 SERVER"). Composed again by
  // the lobby's post-construction attestation/context hand-over.
  net_banner_ms_ = 3000;
  net_refresh_join_banner();
  net_assembler_ = new Net::SnapshotAssembler();
  // B4b: the host's snapshot lists every seat in seat order, so on a 3-4
  // player roster OUR hull isn't necessarily last. The whole client keeps
  // the "local = players->back()" convention (shot reporting, prediction,
  // smoothing, force mirroring — dozens of sites), so rotate our WELCOME
  // seat's ship to the back instead of re-keying every site. At 2P and on
  // seatless pre-v19 rosters back() is already ours: a no-op.
  {
    GLShip *mine = player_by_seat(net_local_seat());
    if (mine && mine != players->back()) {
      players->remove(mine);
      players->push_back(mine);
    }
  }
  // PROTO 14: the local ship reports every shot it fires (id, spawn,
  // exact velocity) so the host spawns clones instead of re-rolling.
  if (!players->empty()) {
    players->back()->ship->net_report_shots = true;
    // PROTO 18: the lance ray-march claims kills instead of killing
    // locally (the claim drain kills — same flow as bullet claims).
    players->back()->ship->net_claim_kills = true;
    NET_LOG("net: shot reporting armed (ship=%p)\n",
            (void *)players->back()->ship);
  }
  NET_LOG("net: ice path %s\n",
         net_session()->transport()->connection_info().c_str());
  // Snapshot restores call Ship::respawn 10x/s; without this its hum
  // start-then-halt leaks random audible blips. The snapshot extras are
  // the only hum authority on a client.
  Ship::net_quiet_respawn = true;

  // The save-restore base constructor bound player-1 keys to the FIRST ship
  // and flagged EVERY saved ship as a local player, but on the client only
  // the rotated-to-back ship is this machine's. Strip bindings and the
  // local-player flag from every other hull (the host's AND, at 3-4P, the
  // other clients' — achievements and lifetime stats must not attribute
  // their actions to this machine) and give the local ship this machine's
  // player-1 controls.
  if (players->size() >= 2) {
    GLShip *local = players->back();
    for (GLShip *gs : *players) {
      if (gs == local) continue;
      gs->ship->is_local_player = false;
      gs->clear_keys();
      gs->set_controller(NULL);
    }
    set_player_keys(local, 0);
    if (controller) local->set_controller(controller);
  }
}

namespace {

template <typename T>
bool nx_read(Save::Stream &in, T &v) { return in.read(&v, sizeof(T)); }

struct NetShipExtras {
  uint8_t alive;
  float temperature;
  int32_t time_until_respawn;
  int32_t time_left_invincible;
  int32_t god_ms;
  uint8_t shield;
  uint8_t warp_count;  // pose-is-absolute signal — see Ship::net_warp_count
  uint8_t move_flags;  // bit0 thrust, bit1 reverse, bits 2-3 rotation (1=L, 2=R)
};

// Which previous copy of a projectile an incoming host one continues, or
// -1 for none. Two passes, and the order matters:
//
// A copy the host has already confirmed wins, within `reach`. An echo
// that can be explained by a projectile the host already had IS that one,
// and letting it claim a fresh local copy instead leaves the older one
// unmatched — read as a detonation that never happened. Nearest-first
// alone is a coin toss the moment a burst puts several in one cluster.
//
// Failing that it takes the nearest previous copy of EITHER kind at ANY
// distance. Every entry in this list belongs to this one ship, so a host
// entry no confirmed copy explains within reach is still the echo of one
// of ours; distance decides which. Unbounded because a just-launched
// unconfirmed copy and its echo drift apart — both seek their own
// targets — and a distance bound left the local one to age out unmatched
// while the echo showed up beside it as a second missile. Either kind
// because a CONFIRMED copy can drift past `reach` in one 100 ms apply
// too: restricting this pass to unconfirmed copies let such an echo
// steal a fresh launch from across the map (teleporting it) while the
// drifted confirmed copy it actually was detonated as a phantom vanish.
// The drifted copy is the nearest to its own echo (they were within
// reach an apply ago; a fresh launch sits at the distant muzzle), so
// nearest-of-any binds both cases correctly.
template <typename T>
int nx_match_previous(const std::vector<T> &old_list, const WrappedPoint &pos,
                      float reach) {
  int best = -1, best_any = -1;
  float best_d = reach, best_any_d = -1.0f;
  for (size_t j = 0; j < old_list.size(); j++) {
    float d = old_list[j].position.distance_to(pos);
    if (best_any < 0 || d < best_any_d) {
      best_any_d = d;
      best_any = (int)j;
    }
    if (!old_list[j].net_unconfirmed && d < best_d) {
      best_d = d;
      best = (int)j;
    }
  }
  return best >= 0 ? best : best_any;
}

// Leftovers of a wholesale projectile rebuild: everything the host's set
// no longer carries. Most of those really did vanish host-side (the
// caller explodes them), but a deploy this machine fired moments ago is
// simply too young to be in the host's snapshot — it flies locally for
// the pilot while the press is still travelling. Holding it (see
// Ship::NET_DEPLOY_GRACE) for a few applies is what stops the client's
// own missile from blowing up at the muzzle a beat before the host's
// echo of the SAME missile arrives and flies. Returns the genuinely
// vanished ones; the held ones are appended back to `live`.
template <typename T>
std::vector<T> nx_hold_unconfirmed(std::vector<T> &old_list,
                                   std::vector<T> &live, bool quiet,
                                   const char *what) {
  std::vector<T> vanished;
  for (auto &o : old_list) {
    if (o.net_unconfirmed == 0) { vanished.push_back(std::move(o)); continue; }
    // A quiet apply is a world rebuild (level rollover) — every projectile
    // goes at once and there is nothing left to be confirmed against.
    if (quiet) continue;
    // Grace spent: the host never fired this one (ammo desync, or we died
    // in between). It leaves silently — it was never the host's to detonate.
    if (--o.net_unconfirmed == 0) {
      NET_LOG("net: %s deploy dropped, no host echo within the grace\n", what);
      continue;
    }
    NET_LOG("net: %s deploy held for the host echo\n", what);
    live.push_back(std::move(o));
  }
  return vanished;
}

// A projectile's four floats, screened before they become an Object: a NaN
// or absurd magnitude here lands in WrappedPoint and then in the collision
// grid, whose out-of-range cell normalization is a per-index loop. Every
// per-message path (MSG_SHOT, the REC_EFFECT bodies) already validates its
// floats; this wholesale section did not. A bad entry is DROPPED rather than
// failing the parse — the asteroid membership records behind this section are
// monotonic facts the caller still wants to reach.
static bool nx_pose_sane(float x, float y, float vx, float vy) {
  return net_coord_sane(x) && net_coord_sane(y) && net_vel_sane(vx) &&
         net_vel_sane(vy);
}

// quiet: suppress vanish explosions for this apply (level rollover wipes
// every projectile at once — that's a rebuild, not a barrage of booms).
// s == NULL: parse-only — advance the stream past the projectile sections
// without touching any ship (the stale-delta membership walk needs the
// removal records that sit AFTER these sections; see net_client_poll).
// own_bullets: this is the CLIENT's OWN ship — its plain bullets are
// locally simulated (fired, stepped, consumed at impacts, expired by
// TTL) and must NOT be replaced by the host's echo: the echo lags a
// round trip, and any shot the host's copy of the gun didn't fire
// (input in flight, cooldown phase off by a step) got WIPED mid-flight
// by the next apply — Glenn's "bullets just disappear soon after they
// leave the ship" — taking its PROTO 13 hit claim with it. Mines/gigas/
// missiles/shockwaves stay host-echoed: they're deployed objects with
// host-side lifecycles, not aim-critical projectiles.
// has_turrets: the stream was written at save version >= 20 and carries the
// turret section (always true on the live wire — PROTO 26 fences builds —
// false only for pre-v20 replay files).
bool nx_read_projectiles(Save::Stream &in, Ship *s, bool quiet,
                         float lead_ms, bool own_bullets = false,
                         bool has_turrets = true) {
  uint16_t n = 0;
  float x, y, vx, vy;

  if (!nx_read(in, n)) return false;
  if (s && !own_bullets) s->bullets.clear();
  for (int i = 0; i < n; i++) {
    uint8_t flags = 0;
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    if (!nx_read(in, flags)) return false;  // PROTO 18 per-bullet flags
    if (!nx_pose_sane(x, y, vx, vy)) continue;
    // Lead by RTT/2: bullets are rebuilt wholesale every apply, and the
    // stale pose strobed against the client's own between-apply stepping
    // (fast movers made it obvious). Mines/gigas are near-stationary and
    // their disappearance matching relies on raw positions — no lead.
    if (s && !own_bullets) {
      s->bullets.push_back(Particle(Point(x + vx * lead_ms, y + vy * lead_ms),
                                    Point(vx, vy), 2000.0f));
      s->bullets.back().set_net_flags(flags);
    }
  }

  if (!nx_read(in, n)) return false;
  // Mines that disappear from the snapshot were detonated by the host
  // (proximity) — play the explosion here. Position-match against the
  // incoming set; mines barely drift, so 100 units is generous.
  std::vector<Particle> old_mines;
  if (s) old_mines.swap(s->mines);
  for (int i = 0; i < n; i++) {
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    if (!s || !nx_pose_sane(x, y, vx, vy)) continue;
    s->mines.push_back(Particle(Point(x, y), Point(vx, vy), 60000.0f));
    int j = nx_match_previous(old_mines, WrappedPoint(x, y), 100.0f);
    if (j >= 0) {
      if (old_mines[j].net_unconfirmed)
        NET_LOG("net: mine deploy confirmed by the host echo\n");
      old_mines.erase(old_mines.begin() + j);
    }
  }
  if (s) {
    std::vector<Particle> gone = nx_hold_unconfirmed(old_mines, s->mines, quiet, "mine");
    if (!quiet)
      for (auto &om : gone) s->net_mine_exploded(om.position, om.velocity);
  }

  if (!nx_read(in, n)) return false;
  std::vector<Particle> old_gigas;
  if (s) old_gigas.swap(s->giga_mines);
  for (int i = 0; i < n; i++) {
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    if (!s || !nx_pose_sane(x, y, vx, vy)) continue;
    s->giga_mines.push_back(Particle(Point(x, y), Point(vx, vy), 60000.0f));
    int j = nx_match_previous(old_gigas, WrappedPoint(x, y), 100.0f);
    if (j >= 0) {
      if (old_gigas[j].net_unconfirmed)
        NET_LOG("net: giga mine deploy confirmed by the host echo\n");
      old_gigas.erase(old_gigas.begin() + j);
    }
  }
  if (s) {
    std::vector<Particle> gone =
        nx_hold_unconfirmed(old_gigas, s->giga_mines, quiet, "giga mine");
    if (!quiet)
      for (auto &og : gone) s->net_giga_mine_exploded(og.position);
  }

  if (!nx_read(in, n)) return false;
  // Missiles carry local presentation state the wire doesn't (trail,
  // thrust ramp): wholesale replacement wiped it 10x/s, so replicated
  // missiles looked like they teleported. Adopt it from the nearest
  // previous missile instead.
  std::vector<MissileShot> old_missiles;
  if (s) old_missiles.swap(s->missiles);
  for (int i = 0; i < n; i++) {
    float fx, fy, time_left;
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy) ||
        !nx_read(in, fx) || !nx_read(in, fy) || !nx_read(in, time_left)) return false;
    if (!s || !nx_pose_sane(x, y, vx, vy) || !std::isfinite(fx) ||
        !std::isfinite(fy) || !std::isfinite(time_left))
      continue;
    MissileShot m(WrappedPoint(x, y), Point(fx, fy), Point(0, 0));
    m.velocity = Point(vx, vy);
    m.time_left = time_left;
    // 150 units: a missile moves well under this per delta.
    int best = nx_match_previous(old_missiles, m.position, 150.0f);
    if (best >= 0) {
      m.trail = std::move(old_missiles[best].trail);
      m.thrust = old_missiles[best].thrust;
      m.sound_handle = old_missiles[best].sound_handle;
      // Our own launch, now echoed back: the host copy takes over from
      // here (and inherits the trail the local one has already drawn).
      if (old_missiles[best].net_unconfirmed)
        NET_LOG("net: missile deploy confirmed by the host echo\n");
      old_missiles.erase(old_missiles.begin() + best);
    }
    s->missiles.push_back(m);
  }
  // A missile that vanished with life remaining exploded on the host
  // (collision) — the expiry case detonates locally in Ship::step. Play
  // the explosion here since the client never simulates the impact. One
  // we fired ourselves a moment ago is held instead: the host's echo of
  // it is still in flight (nx_hold_unconfirmed).
  if (s) {
    std::vector<MissileShot> gone =
        nx_hold_unconfirmed(old_missiles, s->missiles, quiet, "missile");
    if (!quiet)
      for (auto &om : gone)
        if (om.time_left > 300.0f) {
          // The life is the regression handle: a missile that vanishes
          // with nearly all of its 3 s left never flew far enough to hit
          // anything — that is the muzzle-blast bug, not a host kill.
          NET_LOG("net: missile vanished (host detonation), life %d ms\n",
                  (int)om.time_left);
          s->net_missile_exploded(om.position, om.velocity);
        }
  }
  // Fly loop: locally-fired missiles get it from the weapon; replicated
  // ones share one channel per ship, kept alive via the adopted handles
  // and halted automatically when the last holder is destroyed.
  if (s) {
    std::shared_ptr<int> fly;
    for (auto &m : s->missiles)
      if (m.sound_handle) { fly = m.sound_handle; break; }
    for (auto &m : s->missiles) {
      if (!m.sound_handle) {
        if (!fly) fly = s->net_start_missile_fly_loop();
        m.sound_handle = fly;
      }
    }
  }

  if (!nx_read(in, n)) return false;
  size_t old_novas = 0;
  if (s) {
    for (const Shockwave &w : s->shockwaves)
      if (w.is_nova) old_novas++;
    s->shockwaves.clear();
  }
  size_t new_novas = 0;
  for (int i = 0; i < n; i++) {
    float px, py, radius, max_radius, speed, time_left;
    uint8_t is_nova;
    if (!nx_read(in, px) || !nx_read(in, py) || !nx_read(in, radius) ||
        !nx_read(in, max_radius) || !nx_read(in, speed) ||
        !nx_read(in, time_left) || !nx_read(in, is_nova)) return false;
    if (!s) continue;
    // Same screen as the REC_EFFECT ring body (which bounds these already):
    // a ring's radius drives per-frame geometry, its duration a countdown.
    if (!net_coord_sane(px) || !net_coord_sane(py) ||
        !std::isfinite(radius) || !std::isfinite(max_radius) ||
        !std::isfinite(speed) || !std::isfinite(time_left) ||
        max_radius <= 0.0f || max_radius > 100000.0f)
      continue;
    Shockwave w(Point(px, py), max_radius, speed, time_left, is_nova != 0);
    w.radius = radius;
    w.prev_radius = radius;
    s->shockwaves.push_back(w);
    if (is_nova) new_novas++;
  }
  // A nova wave the client hasn't seen yet: the wave itself replicates,
  // only its boom was host-side.
  if (s && !quiet && new_novas > old_novas) s->net_nova_arrived();

  // v20: deployed turrets, host-echoed like mines. A turret that vanishes
  // from the snapshot with real life left was destroyed host-side
  // (asteroid/bullet/hazard contact) — play the debris burst here. One
  // near its natural end is skipped: Ship::step retires it locally on
  // every machine (the missiles' >300 ms trick, wider because the client's
  // copy can sit a snapshot interval behind). A just-deployed local one is
  // held for the host echo (nx_hold_unconfirmed), like every deployable.
  if (!has_turrets) return true;
  if (!nx_read(in, n)) return false;
  std::vector<TurretDrone> old_turrets;
  if (s) old_turrets.swap(s->turrets);
  for (int i = 0; i < n; i++) {
    float aim, ms_left, cooldown;
    uint8_t shots;
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) ||
        !nx_read(in, vy) || !nx_read(in, aim) || !nx_read(in, ms_left) ||
        !nx_read(in, cooldown) || !nx_read(in, shots))
      return false;
    // fields_sane is the shared bound for BOTH turret ingests (this reader
    // and the v21 save restore) — one place, or they drift.
    if (!s || !TurretDrone::fields_sane(x, y, vx, vy, aim, ms_left,
                                        cooldown, shots))
      continue;
    TurretDrone t =
        TurretDrone::from_fields(x, y, vx, vy, aim, ms_left, cooldown, shots);
    int j = nx_match_previous(old_turrets, t.position, 100.0f);
    if (j >= 0) {
      if (old_turrets[j].net_unconfirmed)
        NET_LOG("net: turret deploy confirmed by the host echo\n");
      old_turrets.erase(old_turrets.begin() + j);
    }
    s->turrets.push_back(t);
  }
  if (s) {
    std::vector<TurretDrone> gone =
        nx_hold_unconfirmed(old_turrets, s->turrets, quiet, "turret");
    if (!quiet)
      for (auto &t : gone)
        if (t.ms_left > 1000.0f && t.shots_left > 0) {
          // The life is the regression handle, exactly as for missiles: a
          // turret that vanishes moments after its deploy never met an
          // asteroid — that is the muzzle-blast bug the deploy grace
          // exists to prevent (turret_net.sh asserts on this line).
          NET_LOG("net: turret vanished (host destruction), life %d ms\n",
                  (int)t.ms_left);
          s->turret_explode(t);
        }
  }
  return true;
}

}  // namespace

void GLGame::tick_net_client(int delta) {
  net_tick_t0_ = SDL_GetTicks();  // hitch-breakdown baseline (see below)
  current_time += delta;
  // Anything the local kill() calls queued (ghost removals) is noise —
  // drop it; the host relays the real cues (or the client detects the
  // cosmetic ones itself).
  Ship::net_ship_impacts.clear();
  Ship::net_shots.clear();
  Ship::net_booms.clear();
  // Replay-recorder outboxes: an online client recording drains its OWN
  // fired visuals into the file here (the host's arrive via the MSG_LANCE/
  // MSG_SHOCK/MSG_SHOT receive tees instead); otherwise — playback, whose
  // received replicas never push, or recording disabled — this is just the
  // keep-empty clear it always was.
  replay_drain_effects();
  // Attenuate the peer's self-played sounds (gun, thrusters, death
  // explosion, god-mode tics) by distance to the local camera.
  update_player_sound_volumes();

  if (net_mode_ == NetReplay) {
    // The file is the transport: apply whatever has come due.
    tick_replay_poll(delta);
  } else if (!net_any_peer_lost()) {
    net_client_poll();
    net_ping_tick(delta);
    // Mid-gap marker: the 1 Hz buffered sample can miss a sub-second
    // stall entirely; this fires exactly when silence crosses 300 ms,
    // with our own send-buffer depth at that instant (nonzero = our
    // sender is blocked too, i.e. the stall is bidirectional).
    if (Net::net_debug_enabled() && running && net_last_rx_time_) {
      int quiet = current_time - net_last_rx_time_;
      if (quiet > 300 && !net_peer_make().quiet_logged) {
        net_peer_make().quiet_logged = true;
        NET_LOG("net: rx quiet 300 ms, tx buffered %d bytes\n",
                net_session()->transport()->buffered_amount());
      } else if (quiet <= 300) {
        net_peer_make().quiet_logged = false;
      }
    }
    if (running && net_last_rx_time_ &&
        current_time - net_last_rx_time_ > 10000) {
      NET_LOG("net: RX watchdog - 10 s of silence, connection lost\n");
      net_session()->transport()->close();  // unblock the peer's side too
      net_peer_make().lost = true;
    }
  }
  if (net_mode_ == NetClient && net_session()->transport()->failed())
    net_peer_make().lost = true;
  // Game over is observed here, not simulated: alive/lives replicate, so
  // when the last life goes this side sees it too. The host's tick never
  // runs on a client, so without this the 3 s accidental-exit guard and
  // the local high-score save never armed — and the GAME OVER card (see
  // Overlay::net_overlays) is the client's whole ending, where it used
  // to get only the host's departure (Glenn: "no gameover state for the
  // client, it just disconnects").
  if (!game_over && !players->empty()) {
    bool all_over = true;
    for (auto *gs : *players)
      if (gs->ship->is_alive() || gs->ship->lives > 0) {
        all_over = false;
        break;
      }
    if (all_over) {
      // Watching a replay must never bank a high score — the card still
      // shows (the recorded run really did end here).
      if (net_mode_ == NetClient)
        for (auto *gs : *players) save_high_score(gs->ship->score);
      game_over = true;
      game_over_time = current_time;
      // Client-side recording ends with the run (a no-op in playback:
      // replay_ is null there). finalize's forced final keyframe
      // serializes the replica that just showed the last death, so
      // playback reaches the ended world.
      replay_finish(true);
      board_maybe_start();  // new personal best -> leaderboard prompt
      NET_LOG("net: game over (all players out) - showing GAME OVER card\n");
#ifdef __EMSCRIPTEN__
      EM_ASM(if (window.setMenuMode) window.setMenuMode(1););
      // This block also runs for replay playback (it shares the client
      // tick) — the banner is only for the player's OWN run.
      if (net_mode_ == NetClient) web_notify_game_over(players);
#endif
    }
  }

  // Arm/advance the spectate countdown (client side): our ship out, the
  // host still in it -> after kSpectateDelayMs the camera follows the host.
  update_spectate();

  if (net_all_peers_lost()) {
    // Host gone: snapshots stop, so any snapshot-driven continuous loop
    // (shield hum from spawn invincibility, god-mode music) would be left
    // playing forever. Silence them now — a rejoin restores them from the
    // next snapshot.
    for (auto *gs : *players) gs->ship->silence_loops();
    // If our own ship is already out (we were spectating the host), a lost
    // host is terminal — there is nothing left to rejoin for. End on the
    // GAME OVER card rather than the REJOINING spinner.
    GLShip *me = local_player();
    if (!game_over && me &&
        !me->ship->is_alive() && me->ship->lives <= 0) {
      for (auto *gs : *players) save_high_score(gs->ship->score);
      game_over = true;
      game_over_time = current_time;
      spectate_death_time_ = -1;
      // Terminal for the recording too — nothing will ever append again.
      replay_finish(true);
      board_maybe_start();  // new personal best -> leaderboard prompt
      NET_LOG("net: host lost while spectating - GAME OVER\n");
    }
    // M3-1 auto-rejoin: with a room code the loss is recoverable — hand
    // the game back to a fresh auto-joining lobby (full keyframe
    // bootstrap, the same path as a manual rejoin). Any key/button still
    // exits to the menu via the input handlers. A finished game is the
    // exception: the host leaving AFTER game over is the expected way
    // out, not a loss to recover from — stay on the GAME OVER card.
    if ((!net_room_code_.empty() || !net_lan_host_name_.empty()) &&
        !game_over) {
      if (net_client_rejoin_ms_ <= 0) net_client_rejoin_ms_ = 1500;
      net_client_rejoin_ms_ -= delta;
      if (net_client_rejoin_ms_ <= 0) {
        if (!net_room_code_.empty()) {
          NET_LOG("net: auto-rejoining room %s\n", net_room_code_.c_str());
          net_handed_to_lobby_ = true;  // its warmed ticket outlives us
          request_state_change(new NetLobby(net_room_code_));
        } else {
          // LAN-door session: rediscover the host by name — its GLGame
          // re-beacons on loss (net_host_lan_rejoin_poll), or, if the
          // host app was restarted, its fresh lobby beacons the same
          // name and the rejoin lands in whatever it hosts next.
          NET_LOG("net: auto-rejoining lan host %s\n",
                  net_lan_host_name_.c_str());
          net_handed_to_lobby_ = true;  // its warmed ticket outlives us
          request_state_change(
              new NetLobby(net_lan_host_name_, NetLobby::LanRejoinTag()));
        }
        net_room_code_.clear();  // fire once
        net_lan_host_name_.clear();
      }
    }
    return;  // frozen; draw shows the reconnect notice
  }
  if (net_banner_ms_ > 0) net_banner_ms_ -= delta;

  if (!running) {
    last_tick += delta;
    net_last_rx_time_ = current_time;  // paused peers legitimately go quiet
    net_last_send_time_ = current_time;  // no sends while paused = no stall
    // Pauses freeze the host clock too: shift the anchor so paused time
    // doesn't count into the derived estimate below.
    if (net_est_anchor_local_ >= 0) net_est_anchor_local_ += delta;
    return;
  }
  // The host's clock runs whenever we do (pauses stop both), so the
  // stale-delta estimate is the last accepted apply's host clock plus
  // local running time since it arrived. DERIVED, not accumulated: a
  // "+= delta" here after a poll that just re-anchored double-counted
  // client-side hitches and wedged the gate shut (see glgame.h).
  if (net_est_anchor_host_ >= 0)
    net_host_est_ = net_est_anchor_host_ + (current_time - net_est_anchor_local_);

  // End of the recording: freeze the world — extrapolating past the last
  // record would invent a future the file never contained. The HUD shows
  // the ended state; Esc (or the game-over card's flow) exits. Engine audio
  // off: the flags freeze in their last state and would drone forever.
  if (net_mode_ == NetReplay && replay_finished_) {
    if (!players->empty() && players->front()->ship->boost_sound != NULL)
      Mix_VolumeChunk(players->front()->ship->boost_sound, 0);
    last_tick += delta;
    return;
  }

  // (Ghost engine audio — REPLAY.md R2 — used to be a special case here: the
  // boost loop's volume was driven only by the local input methods, and the
  // extras write ghost flags raw, so a replay drove one shared chunk from
  // "is ANY ghost under power". update_player_sound_volumes above now levels
  // every ship's own loop from its own replicated flags, which is the same
  // thing done per ghost.)

  // Hitch breakdown: "net: frame hitch" (tick()) names the stall but not
  // the culprit — split this tick into poll (applies) vs step-loop time,
  // and pair it with the "slow draw" line to localize a slow frame.
  uint32_t hb_poll_ms = SDL_GetTicks() - net_tick_t0_;
  uint32_t hb_steps_t0 = SDL_GetTicks();
  int hb_steps = 0;

  time_until_next_step -= delta;
  while (time_until_next_step <= 0) {
    hb_steps++;
    // Visual/kinematic stepping only: motion, particles, trails, timers.
    // No collisions, kills, drops or generation logic — the host simulates
    // and its snapshots overwrite this extrapolation at 10 Hz.
    for (auto *bh : *black_holes) bh->step(step_size);
    for (auto *a : *objects) {
      a->step(step_size);
      net_decay_render_offset(*a, step_size,
                              players->back()->ship->position);
      // Render-jump detector: the DRAWN pose may only move by motion,
      // gravity, mirrored bounces and offset decay — log any step that
      // exceeds that budget several-fold, because that is the literal
      // jitter the player sees and no reconcile counter measures it.
      if (Net::net_debug_enabled()) {
        static std::unordered_map<uint32_t, Point> s_prev;
        WrappedPoint r(a->position.x() + a->net_pose_err.x(),
                       a->position.y() + a->net_pose_err.y());
        r.wrap();
        std::unordered_map<uint32_t, Point>::iterator pi = s_prev.find(a->net_id);
        if (pi != s_prev.end()) {
          Point c = r.closest_to(pi->second);
          float jx = c.x() - pi->second.x(), jy = c.y() - pi->second.y();
          float jump = sqrtf(jx * jx + jy * jy);
          float off = sqrtf(a->net_pose_err.x() * a->net_pose_err.x() +
                            a->net_pose_err.y() * a->net_pose_err.y());
          float budget = a->velocity.magnitude() * step_size + off * 0.15f + 2.0f;
          if (jump > budget * 3.0f)
            NET_LOG("net: render jump %.0f id=%u (spd %.2f off %.0f budget %.0f)\n",
                    jump, a->net_id, a->velocity.magnitude(), off, budget);
        }
        s_prev[a->net_id] = Point(r.x(), r.y());
      }
    }
    // Mirror the host's asteroid gravity so drift between 1 Hz keyframes
    // stays small — otherwise every asteroid near a hole goes stale.
    for (auto *bh : *black_holes)
      for (auto *a : *objects)
        if (!a->invincible) bh->apply_gravity(*a, step_size);
    // Mirror the host's elastic bounces too (silently), or every one of
    // them is a surprise the records correct 100 ms later.
    elastic_asteroid_collisions(/*announce=*/false);
    // ...and the quantum observation flips (4x speed changes), the last
    // host-only velocity rule the client couldn't predict.
    update_quantum_observation();
    for (auto *a : *dead_objects) a->step(step_size);
    for (auto *p : *pickups) p->step(step_size);
    // The HOST owns respawns. Ship::step self-respawns when the local
    // countdown crosses zero — at a RANDOM position (and charging a
    // life), racing the authoritative snapshot by tick skew; the ghost
    // then stood at the random spot until the host's real spawn arrived.
    // Hold the countdown just above zero; the snapshot's alive-transition
    // resurrects the ship at the host's spawn position.
    for (auto *gs : *players) {
      Ship *sh = gs->ship;
      if (!sh->is_alive() && sh->time_until_respawn <= (int)step_size)
        sh->time_until_respawn = step_size + 1;
    }
    // Refresh the shock-bolt seek list before stepping the local ship, exactly
    // as the host's tick() does — without this the client's shock arc seeks
    // ONLY asteroids (missile list) and sails straight past every enemy,
    // station, mini-station and hazard, so a client-fired bolt could never
    // reach (or claim) them. Damage stays where it belongs: enemies are
    // client-claimed below, station/mini/hazard hull is host-authoritative.
    shock_targets->clear();
    for (auto *ge : *enemies)
      shock_targets->push_back(ge->ship);
    if (station != NULL && station->is_alive())
      shock_targets->push_back(station);
    if (mini_station != NULL && mini_station->is_alive())
      shock_targets->push_back(mini_station);
    for (auto *h : *hazards)
      if (h->is_alive())
        shock_targets->push_back(h);
    if (friendly_fire)
      for (auto *gs : *players)
        shock_targets->push_back(gs->ship);

    for (auto *gs : *players) gs->step(step_size, grid);
    {
      // Same one-tic-per-step drain as the offline/host loop (see
      // Ship::respawn_tic_pending) — only the local ship has own-cues
      // here, but the flag must drain wherever players step or it plays
      // nothing at all.
      bool tic_played = false, low_played = false;
      for (auto *gs : *players)
        gs->ship->flush_respawn_tic(tic_played, low_played);
    }
    // The remote (host) ship reconciles like the asteroids; the LOCAL
    // ship never banks an error (its pose is authoritative, v12), so
    // this is a no-op for it.
    for (auto *gs : *players) {
      net_smooth_step(*gs->ship, step_size);
      net_smooth_facing(*gs->ship, step_size);
    }
    // v12: with pose authority the black hole must pull the pilot HERE —
    // the host's pull is overwritten by every adopted INPUT. Same
    // reduced-pull rule as the host's ship loop; the event-horizon kill
    // stays host-side (ignore the return — death arrives as a snapshot
    // alive-transition). NetClient only: replay ghosts have no authoritative
    // pilot — their poses are wholly record-driven, and a local force would
    // fight the recorded outcome.
    if (net_mode_ == NetClient) {
      Ship *me = players->back()->ship;
      if (me->is_alive()) {
        float scale = (me->god_mode_time_remaining() > 0 ||
                       me->shield_active()) ? 0.25f : 1.0f;
        for (auto *bh : *black_holes)
          bh->apply_gravity(*me, step_size, scale);
      }
    }
    // v12 pose authority: the pulsar shockwave's outward SHOVE is a force on
    // the local ship, so — like the black-hole pull above — it must be applied
    // HERE. The host applies it to its copy of our ship, but our authoritative
    // pose discards that every INPUT, so without this mirror the wave passed
    // straight through the client's pilot ("the pulsar doesn't do anything").
    // The push flings you into an asteroid; the host resolves the resulting
    // collision and the death replicates. Matches the host loop's knockback
    // (glgame.cpp COLLIDE SHIPS ... HAZARDS); the client's pulsar timer is
    // reconciled from the snapshot, so wave_active()/wave_radius() line up.
    // NetClient only — same ghost rule as the black-hole mirror above.
    if (net_mode_ == NetClient) {
      Ship *me = players->back()->ship;
      if (me->is_alive()) {
        for (auto *h : *hazards) {
          if (h->kind_of() != Hazard::PULSAR || !h->is_alive() || !h->wave_active())
            continue;
          float wr = h->wave_radius();
          if (fabsf(me->position.distance_to(h->position) - wr) > Hazard::WAVE_BAND)
            continue;
          Point self = h->position.closest_to(me->position);
          Point out(me->position.x() - self.x(), me->position.y() - self.y());
          float m = out.magnitude();
          if (m > 1e-4f) me->velocity += (out / m) * (Hazard::KNOCKBACK * step_size);
        }
      }
    }
    // Mini-station: drift/spin/bullets extrapolate between snapshots —
    // without this it visibly teleported at the 10 Hz apply rate.
    if (mini_station) mini_station->net_client_step(step_size);
    // Gen-20 station + its enemies: kinematic-only extrapolation (no AI,
    // no firing — the host owns those, the snapshot reconciles).
    if (station) station->net_client_step(step_size);
    for (auto *e : *enemies) {
      e->ship->Object::step(step_size);
      for (auto &b : e->ship->bullets) b.step(step_size);
    }
    // Mid-game hazards: extrapolate motion + animation between snapshots
    // (pulsar cycle, comet drift + trail, seeker homing) so they glide
    // rather than teleport at the apply rate — visual only; the lethal
    // effects and deaths are host-authoritative and reconcile via the
    // snapshot. Reap replicas whose death burst has finished fading.
    for (auto hi = hazards->begin(); hi != hazards->end();) {
      (*hi)->update(step_size, players);
      if ((*hi)->is_removable()) { delete *hi; hi = hazards->erase(hi); }
      else ++hi;
    }
    // Cosmetic bullet-vs-hazard impacts: hazards aren't in the collision grid,
    // so net_cosmetic_impacts (below) misses them and a shot would sail
    // straight through the pulsar/comet on the client. Consume the bullet at
    // contact with a spark + thud, mirroring the host's hit — the real damage
    // is host-authoritative (the client's shot rides to the host as a MSG_SHOT
    // clone; the break-up arrives as the replica dropping from the snapshot).
    if (!hazards->empty()) {
      for (auto *gs : *players) {
        Ship *sh = gs->ship;
        for (size_t bi = 0; bi < sh->bullets.size();) {
          bool hit = false;
          for (auto *h : *hazards) {
            if (!h->is_alive()) continue;
            if (h->Object::collide(sh->bullets[bi])) {
              sh->explode(sh->bullets[bi].position, Point());
              WorldSound::play(Asteroid::thud_sound, sh->bullets[bi].position);
              hit = true;
              break;
            }
          }
          if (hit) {
            sh->bullets[bi] = std::move(sh->bullets.back());
            sh->bullets.pop_back();
          } else {
            ++bi;
          }
        }
      }
    }
    grid.update((std::list<Object *> *)objects);
    // Cosmetic bullet-vs-asteroid impacts (debris + thud/ting for
    // asteroids a bullet can't kill), detected locally against the fresh
    // grid — replaces the host's EV_ROID impact events (PROTO 10). The
    // local ship also claims its would-kill hits for the confirmation
    // telemetry below.
    for (auto *gs : *players)
      gs->ship->net_cosmetic_impacts(
          grid, net_mode_ == NetClient && gs == players->back());
    // PROTO 15: the local ship's bullets vs replicated enemy ships and
    // stations — they used to sail straight through (enemies aren't in
    // the asteroid grid). Consume at contact + claim to the host. The
    // whole block is claim machinery for the LOCAL ship — a replay has
    // none (ghost hits resolve in the records themselves).
    if (net_mode_ == NetClient) {
      std::vector<Ship::NetShipTarget> ship_targets;
      for (auto *ge : *enemies)
        if (ge->ship->is_alive())
          ship_targets.push_back(
              {(Object *)ge->ship, (uint8_t)0, ge->ship->net_ship_id});
      if (station != NULL && station->is_alive())
        ship_targets.push_back({(Object *)station, (uint8_t)1, 0u});
      if (mini_station != NULL && mini_station->is_alive())
        ship_targets.push_back({(Object *)mini_station, (uint8_t)2, 0u});
      players->back()->ship->net_cosmetic_ship_impacts(ship_targets);

      // PROTO 20: the local lance vs enemy replicas — the same instant
      // kill + claim treatment bullets get (kind 0 with bullet_id 0: a
      // lance has no clone to consume, mirroring MSG_HIT's sentinel).
      // Station/mini hull hits stay host-side (resolved from the
      // MSG_LANCE polyline): hull damage is not idempotent, so it must
      // apply exactly once — enemy kills are idempotent by wire id.
      Ship *lme = players->back()->ship;
      if (!lme->lance_hit_pending.empty()) {
        for (auto &t : ship_targets) {
          Ship *e = static_cast<Ship *>(t.obj);
          if (!e->is_alive()) continue;
          Point where;
          bool hit = false;
          for (size_t s = 0; s + 1 < lme->lance_hit_pending.size() && !hit; s++)
            hit = lance_seg_circle_entry(lme->lance_hit_pending[s],
                                         lme->lance_hit_pending[s + 1],
                                         e->position, e->radius, &where);
          if (!hit) continue;
          if (t.kind != 0) {
            // Station/mini hull contact: cosmetic only — local debris
            // splash (the host's splash spawns on OUR replica over there
            // and never replicates back) plus the deflection thud for the
            // station. Damage/destruction stay host-side via MSG_LANCE,
            // and the host skips its EV_ROID_THUD relay for our polyline
            // so this cue isn't doubled.
            lme->explode(where, e->velocity);
            if (t.kind == 1) WorldSound::play(Asteroid::thud_sound, where);
            continue;
          }
          if (e->kill_stop()) {
            e->detonate();
            lme->credit_ship_kill(e);  // enemies_10/score, ours
          }
          // Same silent-replica rule as the bullet path: play the dying
          // ship's boom ourselves, full volume — the kill is at our own
          // crosshair (the host skips its relay for claimed kills).
          if (e->explode_sound != NULL) {
            Mix_VolumeChunk(e->explode_sound, MIX_MAX_VOLUME);
            Mix_PlayChannel(-1, e->explode_sound, 0);
          }
          Ship::NetShipHit c;
          c.kind = 0;
          c.bullet_id = 0;  // lance sentinel: honor without a clone consume
          c.target_id = t.id;
          c.x = where.x();
          c.y = where.y();
          Ship::net_ship_hit_claims.push_back(c);
          NET_LOG("net: lance enemy kill claimed id=%u\n", t.id);
        }
        lme->lance_hit_pending.clear();
      }

      // PROTO 22: resolve the local shock bolts' hits. A bolt's struck list
      // holds the exact objects its arc reached this frame. This MUST fully
      // drain struck every tick: the client never calls Ship::collide_grid
      // (it uses net_cosmetic_impacts instead), so an entry left here dangles
      // once net_apply_state frees the asteroid a few frames later — the next
      // dynamic_cast then segfaults on freed memory (Glenn's joiner crash).
      //   Asteroids: claim the kill with a bullet_id-0 MSG_HIT (the lance
      //     sentinel — one-shot, since the host suppresses the remote ship's
      //     shock sim and so can't chip tough rocks itself; the claim drain
      //     kills our copy this tick and the host honors it exactly-once).
      //   Enemies: instant kill + bullet_id-0 ship claim, like the lance.
      //   Station / mini: cosmetic splash only; hull damage stays host-side,
      //     applied from the MSG_SHOCK polyline in the host's handler.
      Ship *sme = players->back()->ship;
      for (auto &bolt : sme->shocks) {
        for (Object *obj : bolt.struck) {
          if (Asteroid *a = dynamic_cast<Asteroid *>(obj)) {
            // Invincible asteroids never appear here: they are not sought
            // and grow_segment stops the bolt at their surface without a
            // struck entry. (Even if one slipped through, the host's MSG_HIT
            // handler refuses claims on invincible rocks.)
            if (a->alive) {
              // Only claim what our LOCAL rules say a shock hit KILLS — the
              // bullet-claim principle. A survivor (ready-to-teleport,
              // phased ghost, tough with health left) used to be claimed
              // anyway, and the host's claim handler FORCES claims through
              // (vulnerable/health=1/unphased), so a client's lightning
              // one-shot rocks the host's own bolts could not (Glenn,
              // 2026-08-02: make it offline-consistent). Now: stop the arc
              // like offline collide_grid does; the real outcome — teleport
              // evade, tough chip — is the host's, resolved from our
              // MSG_SHOCK polyline endpoint, and arrives via the snapshot.
              if ((a->teleporting && !a->teleport_vulnerable) ||
                  (a->phasing && a->phased) ||
                  (a->tough && a->health > 1)) {
                sme->explode(a->position, a->velocity);  // spark feedback
                bolt.stop();
              } else {
                Ship::NetKillClaim c;
                c.ast_id = a->net_id;
                c.bullet_id = 0;
                Ship::net_kill_claims.push_back(c);
              }
            }
            continue;
          }
          // Mid-game hazards: hull damage is host-authoritative (resolved from
          // our MSG_SHOCK polyline over there), so this is cosmetic + the arc
          // stop() rule only. A comet/pulsar absorbs the bolt and halts it; a
          // one-shot seeker lets it chain onward. Never claimed here — the real
          // destruction arrives as the replica dropping from the snapshot.
          if (Hazard *hz = dynamic_cast<Hazard *>(obj)) {
            if (hz->is_alive()) {
              sme->explode(hz->position, hz->velocity);
              WorldSound::play(Asteroid::thud_sound, hz->position);
              if (hz->kind_of() != Hazard::SEEKER)
                bolt.stop();
            }
            continue;
          }
          // Friendly fire: the arc reached the partner (host) ship. Its damage
          // is host-authoritative too (resolved from the polyline), so stop the
          // arc cosmetically and let the host decide the kill.
          if (friendly_fire && !players->empty() &&
              obj == players->front()->ship) {
            if (players->front()->ship->is_alive()) {
              sme->explode(players->front()->ship->position,
                           players->front()->ship->velocity);
              bolt.stop();
            }
            continue;
          }
          const Ship::NetShipTarget *t = NULL;
          for (auto &st : ship_targets) if (st.obj == obj) { t = &st; break; }
          if (t == NULL) continue;
          Ship *e = static_cast<Ship *>(t->obj);
          if (!e->is_alive()) continue;
          if (t->kind != 0) {
            // Station / mini hull: cosmetic only; damage is host-authoritative.
            // The station/mini survive multiple hits, so the arc stops here.
            sme->explode(e->position, e->velocity);
            if (t->kind == 1) WorldSound::play(Asteroid::thud_sound, e->position);
            bolt.stop();
            continue;
          }
          if (e->kill_stop()) {
            e->detonate();
            sme->credit_ship_kill(e);
          } else {
            bolt.stop();  // enemy survived (invincible) — arc ends here
          }
          if (e->explode_sound != NULL) {
            Mix_VolumeChunk(e->explode_sound, MIX_MAX_VOLUME);
            Mix_PlayChannel(-1, e->explode_sound, 0);
          }
          Ship::NetShipHit c;
          c.kind = 0;
          c.bullet_id = 0;  // shock sentinel: honor without a clone consume
          c.target_id = t->id;
          c.x = e->position.x();
          c.y = e->position.y();
          Ship::net_ship_hit_claims.push_back(c);
          NET_LOG("net: shock enemy kill claimed id=%u\n", t->id);
        }
        bolt.struck.clear();  // fully drained — never carry a pointer to next frame
      }
    }

    // Local-ship render-jump detector: the camera rides this pose, so a
    // single-step discontinuity here moves the WHOLE visible field in
    // unison — "all the asteroids jump at the same time" while every
    // asteroid diagnostic stays silent.
    if (Net::net_debug_enabled()) {
      static Point s_prev_ship;
      static bool s_have_prev = false;
      Ship *me = players->back()->ship;
      if (me->is_alive()) {
        Point cur(me->position.x(), me->position.y());
        if (s_have_prev) {
          Point c = me->position.closest_to(s_prev_ship);
          float jx = c.x() - s_prev_ship.x(), jy = c.y() - s_prev_ship.y();
          float jump = sqrtf(jx * jx + jy * jy);
          float budget = me->velocity.magnitude() * step_size + 3.0f;
          if (jump > budget * 3.0f)
            NET_LOG("net: SHIP render jump %.0f (spd %.2f)\n",
                    jump, me->velocity.magnitude());
        }
        s_prev_ship = cur;
        s_have_prev = true;
      } else {
        s_have_prev = false;
      }
    }

    if (net_mode_ == NetClient) net_client_send_input();
    // Time-slow mirror (PROTO 24): the host's snapshots carry the countdown
    // and net_apply_state adopted it — pace THIS loop by the same factor so
    // the extrapolation (and the local ship's sim) advances at the host's
    // slowed wall rate instead of overshooting into every reconcile. The
    // per-step countdown keeps the window honest between applies; replays
    // ride the same mirror against their recorded wall-clock spacing.
    time_slow_step();
    time_until_next_step += time_between_steps *
        (time_slow_active() ? kTimeSlowFactor : 1);
  }
  if (Net::net_debug_enabled()) {
    uint32_t steps_ms = SDL_GetTicks() - hb_steps_t0;
    if (hb_poll_ms + steps_ms > 25)
      NET_LOG("net: slow tick: poll %u ms, %d steps %u ms\n",
              hb_poll_ms, hb_steps, steps_ms);
  }

  // Reap faded kill remains — NOT client-only machinery: the score value
  // drawn on a dead asteroid lives exactly as long as its dead_objects
  // entry, so skipping this in playback left every kill's score (and
  // debris) on screen forever (Glenn's Android report; it sat below the
  // NetReplay early-out until it moved here, above it).
  {
    auto oi = dead_objects->begin();
    while (oi != dead_objects->end()) {
      if ((*oi)->is_removable()) {
        delete *oi;
        oi = dead_objects->erase(oi);
      } else {
        oi++;
      }
    }
  }

  // Everything below is the client's outbound report/claim machinery
  // (MSG_SHOT/LANCE/SHOCK/HIT/HIT_SHIP over the session). A replay has no
  // session and its ghosts never arm the reporting flags, so the vectors
  // stay empty — but clear them explicitly rather than trust that forever.
  if (net_mode_ != NetClient) {
    Ship::net_shot_reports.clear();
    Ship::net_lance_reports.clear();
    Ship::net_shock_reports.clear();
    Ship::net_ship_hit_claims.clear();
    Ship::net_kill_claims.clear();
    return;
  }

  // PROTO 25: outgoing messages stamp this machine's real seat (the
  // WELCOME assignment) instead of the literal 2.
  uint8_t my_seat = (uint8_t)net_local_seat();

  // Client hit-authority (PROTO 13): every would-kill consume detected
  // in the step loop kills the asteroid HERE — instantly, even mid-stall
  // — and sends a reliable MSG_HIT claim the host honors. Fragments,
  // drops and score stay host-owned and replicate back as ordinary new
  // records / HUD scalars; the only local fiction is the parent's final
  // explosion at this screen's pose. "No more shots that don't count."
  // PROTO 14: report every shot the local ship fired this tick — the
  // host spawns exact clones (same spawn point, same spread-applied
  // velocity, same id) instead of re-rolling its own gun sim.
  for (const Ship::NetShotReport &r : Ship::net_shot_reports) {
    std::vector<uint8_t> msg;
    Net::put_header(msg, Net::MSG_SHOT, my_seat);
    Net::put_u32(msg, r.id);
    Net::put_f32(msg, r.x);
    Net::put_f32(msg, r.y);
    Net::put_f32(msg, r.vx);
    Net::put_f32(msg, r.vy);
    Net::put_u8(msg, (r.kills_invincible ? 1 : 0) | (r.has_trail ? 2 : 0) |
                       (r.piercing ? 4 : 0));
    net_session()->transport()->send_reliable(&msg[0], msg.size());
    // 1 Hz send-side twin of the host's "reported shots/s spawned" —
    // a rate mismatch between the two lines localizes a report leak.
    if (Net::net_debug_enabled()) {
      static uint32_t s_last = 0;
      static int s_count = 0;
      s_count++;
      uint32_t now = SDL_GetTicks();
      if (now - s_last >= 1000) {
        NET_LOG("net: %d shot reports/s sent (last id=%u)\n", s_count, r.id);
        s_last = now;
        s_count = 0;
      }
    }
  }
  Ship::net_shot_reports.clear();

  // PROTO 18: fired lance pulses — the traced polyline for the host's
  // flash + sound (the kills ride the MSG_HIT claims below).
  for (const auto &rep : Ship::net_lance_reports) {
    const std::vector<Point> &pts = rep.second;
    if (pts.size() < 2 || pts.size() > 17) continue;
    std::vector<uint8_t> msg;
    Net::put_header(msg, Net::MSG_LANCE,
                    rep.first && rep.first->net_seat ? rep.first->net_seat
                                                     : my_seat);
    Net::put_u8(msg, (uint8_t)pts.size());
    for (const Point &p : pts) {
      Net::put_f32(msg, p.x());
      Net::put_f32(msg, p.y());
    }
    net_session()->transport()->send_reliable(&msg[0], msg.size());
  }
  Ship::net_lance_reports.clear();

  // PROTO 22: completed shock bolts — the peer shows the firer's EXACT
  // segments (a re-seek would diverge). Kills ride the MSG_HIT/MSG_HIT_SHIP
  // claims; station/mini hull damage is applied from this polyline.
  for (const auto &rep : Ship::net_shock_reports) {
    const std::vector<Point> &pts = rep.second;
    if (pts.size() < 2 || pts.size() > 15) continue;
    std::vector<uint8_t> msg;
    Net::put_header(msg, Net::MSG_SHOCK,
                    rep.first && rep.first->net_seat ? rep.first->net_seat
                                                     : my_seat);
    Net::put_u8(msg, (uint8_t)pts.size());
    for (const Point &p : pts) {
      Net::put_f32(msg, p.x());
      Net::put_f32(msg, p.y());
    }
    net_session()->transport()->send_reliable(&msg[0], msg.size());
  }
  Ship::net_shock_reports.clear();

  // PROTO 15/16: ship/station hit claims — damage applies host-side IFF
  // the referenced clone gets consumed there (exactly-once per shot).
  for (const Ship::NetShipHit &c : Ship::net_ship_hit_claims) {
    std::vector<uint8_t> msg;
    Net::put_header(msg, Net::MSG_HIT_SHIP, my_seat);
    Net::put_u8(msg, c.kind);
    Net::put_u32(msg, c.bullet_id);
    Net::put_u32(msg, c.target_id);
    Net::put_f32(msg, c.x);
    Net::put_f32(msg, c.y);
    net_session()->transport()->send_reliable(&msg[0], msg.size());
    // The enemy died locally the moment the bullet connected — keep the
    // restores from resurrecting it while the claim is in flight.
    if (c.kind == 0 && c.target_id != 0)
      net_predicted_ship_kills_[c.target_id] = current_time + 3000;
  }
  Ship::net_ship_hit_claims.clear();

  if (!Ship::net_kill_claims.empty()) {
    for (const Ship::NetKillClaim &c : Ship::net_kill_claims) {
      // Several bullets (or several steps of this tick) can consume
      // against the same rock before the drain kills it — one claim is
      // enough, the rest are duplicates.
      std::map<uint32_t, int>::iterator dup =
          net_predicted_kills_.find(c.ast_id);
      if (dup != net_predicted_kills_.end() && current_time < dup->second)
        continue;
      std::vector<uint8_t> msg;
      Net::put_header(msg, Net::MSG_HIT, my_seat);
      Net::put_u32(msg, c.ast_id);
      Net::put_u32(msg, c.bullet_id);
      net_session()->transport()->send_reliable(&msg[0], msg.size());
      // Keyframes cut before the host processes the claim still list
      // this id — suppress re-creation until the removal propagates.
      net_predicted_kills_[c.ast_id] = current_time + 3000;
      for (auto oi = objects->begin(); oi != objects->end(); ++oi) {
        Asteroid *a = *oi;
        if (a->net_id != c.ast_id) continue;
        // Force the same divergent state the host's claim handler forces:
        // a lance claim (PROTO 18) can name a tough asteroid — kill() must
        // actually kill here or the local copy vanishes without exploding.
        if (a->tough) a->health = 1;
        if (a->teleporting) a->teleport_vulnerable = true;
        a->phased = false;
        a->kill();  // claim conditions guarantee the kill lands
        // Our claimed kill: achievements + lifetime stats credit on THIS
        // machine (first_kill, kills_1000, specials_7, stats.dat...). The
        // host credits its replica's counters too; the snapshot replaces
        // our local increments with the authoritative count. No nova
        // feedback — nova charge and drops are host-owned state.
        if (!players->empty())
          players->back()->ship->credit_asteroid_kill(a, false);
        if (!a->is_alive() && !a->is_removable()) {
          Asteroid::play_explode_sound(a->position);
          dead_objects->push_back(a);
        } else {
          delete a;
        }
        objects->erase(oi);
        break;
      }
      NET_LOG("net: hit claim sent id=%u (killed locally)\n", c.ast_id);
    }
    Ship::net_kill_claims.clear();
    grid.update((std::list<Object *> *)objects);  // no dangling grid entries
  }
  // Expire suppression entries the removal record has long since covered.
  for (auto pk = net_predicted_kills_.begin();
       pk != net_predicted_kills_.end();)
    pk = current_time >= pk->second ? net_predicted_kills_.erase(pk) : ++pk;
  for (auto pk = net_predicted_ship_kills_.begin();
       pk != net_predicted_ship_kills_.end();)
    pk = current_time >= pk->second ? net_predicted_ship_kills_.erase(pk)
                                    : ++pk;
}

void GLGame::net_client_send_input() {
  // Production-side half of the host's input-gap forensics: if THIS log
  // fires at the same moment the host logs a gap, the client simply
  // stopped stepping (frame stall, app nap) — no network involved.
  if (net_last_send_time_ && current_time - net_last_send_time_ > 300)
    NET_LOG("net: input send stall %d ms (client-side)\n",
            current_time - net_last_send_time_);
  net_last_send_time_ = current_time;

  GLShip *local = players->back();
  Ship *s = local->ship;

  Net::InputState in;
  in.seq = ++net_input_seq_;
  uint16_t held = 0;
  if (s->rotation_direction == Ship::LEFT) held |= Net::IN_LEFT;
  if (s->rotation_direction == Ship::RIGHT) held |= Net::IN_RIGHT;
  if (s->thrusting) held |= Net::IN_THRUST;
  if (s->reversing) held |= Net::IN_REVERSE;
  // Fire triggers come from key intent (GLShip), not the weapon: a
  // semi-automatic weapon clears its own trigger after every shot, which
  // would read as "not shooting" in almost every INPUT message.
  if (local->net_shoot_held) held |= Net::IN_SHOOT;
  if (local->net_secondary_held) held |= Net::IN_SECONDARY;
  in.held = held;
  in.boost_count = s->net_boost_count;
  in.next_weapon_count = s->net_next_weapon_count;
  in.next_secondary_count = s->net_next_secondary_count;
  in.teleport_count = s->net_teleport_count;
  in.respawn_count = local->net_respawn_count;
  in.shoot_press_count = local->net_shoot_press_count;
  in.secondary_press_count = local->net_secondary_press_count;
  in.analog_rotation = s->rotation_scale;
  in.analog_thrust = s->thrust_analog;
  in.analog_reverse = s->reverse_analog;
  in.facing_x = s->facing.x();
  in.facing_y = s->facing.y();
  // Client-authoritative pose (v12): report the exact pose this screen
  // shows; the echoed warp count proves it postdates any host-driven
  // respawn/teleport (net_prev_warp_ tracks the last one seen).
  in.pos_x = s->position.x();
  in.pos_y = s->position.y();
  in.vel_x = s->velocity.x();
  in.vel_y = s->velocity.y();
  in.warp_echo = net_prev_warp_;

  std::vector<uint8_t> msg;
  Net::encode_input(msg, in, (uint8_t)net_local_seat());
  net_session()->transport()->send_unreliable(&msg[0], msg.size());

  // Reliable mirror (see net_mirror_* in glgame.h): every input CHANGE
  // rides the ordered channel too, so a lost release can be late but
  // never gone; a 10 Hz refresh covers the analog-only drift between
  // changes. ~40 bytes a shot — noise next to the 10 Hz deltas.
  uint8_t counts = (uint8_t)(in.boost_count + in.next_weapon_count +
                             in.next_secondary_count + in.teleport_count +
                             in.respawn_count + in.shoot_press_count +
                             in.secondary_press_count);
  bool changed = in.held != net_mirror_held_ || counts != net_mirror_counts_;
  if (changed || ++net_mirror_steps_ >= 12) {
    net_session()->transport()->send_reliable(&msg[0], msg.size());
    net_mirror_steps_ = 0;
    net_mirror_held_ = in.held;
    net_mirror_counts_ = counts;
  }
}

void GLGame::net_client_poll() {
  NetTransport *t = net_session()->transport();
  // Flood guard, symmetric with net_host_poll: the host is an untrusted peer
  // too, and its SHOT/LANCE/SHOCK echoes each allocate (bullets/lance_pulses/
  // shocks) on our side. Same two budgets — a read-loop cap so a message storm
  // can't spin, and a shared action budget across the spawn family. NEVER
  // gated: the state stream (DELTA/SNAPSHOT_CHUNK — essential and already
  // stale-gated) and EVENT (reliable+ordered and stateful: a dropped
  // EV_PAUSE/EV_BYE/EV_PICKUP is consumed from SCTP, never redelivered, and
  // no snapshot reconciles it; a post-stall backlog can legitimately exceed
  // the budget in one drain. EVENT's heavy codes are bounded separately —
  // net_event_effect_budget_). PING stays counted (bounds the pong
  // reflection; worst case the RTT lead stales until the flood ends).
  const int NET_MAX_MSGS_PER_POLL = 512;
  const int NET_MAX_ACTIONS_PER_POLL = 64;
  int net_msgs_seen = 0, net_actions = 0;
  bool net_action_cap_logged = false;
  net_event_effect_budget_ = NET_EVENT_EFFECTS_PER_POLL;

  std::vector<unsigned char> msg;
  while (t->poll(msg)) {
    if (++net_msgs_seen > NET_MAX_MSGS_PER_POLL) {
      NET_LOG("net: rx message storm - %d msgs, deferring rest\n",
              net_msgs_seen);
      break;
    }
    // RX-side half of the gap forensics: fires when OUR inbound stream
    // resumes after silence — pairs with the host's "input gap" line to
    // show whether an outage was bidirectional (path) or one-way.
    if (running && net_last_rx_time_ &&
        current_time - net_last_rx_time_ > 300)
      NET_LOG("net: rx gap %d ms ended\n", current_time - net_last_rx_time_);
    net_last_rx_time_ = current_time;  // any arrival feeds the watchdog
    Net::Reader r(msg.empty() ? nullptr : &msg[0], msg.size());
    Net::Header h;
    if (!Net::read_header(r, h)) continue;
    // MSG_PEER_IDENT joins the exemption list for EVENT's exact reason:
    // reliable + consumed-once with NO periodic re-send (it re-fires only
    // on a first INPUT or an attestation change), so a post-stall drain
    // eating the burst would leave role labels on seats 3-4 indefinitely.
    // Bounded like EVENT is: ~4 tiny messages per roster change.
    if (h.msg_type != Net::MSG_DELTA && h.msg_type != Net::MSG_SNAPSHOT_CHUNK &&
        h.msg_type != Net::MSG_EVENT && h.msg_type != Net::MSG_PEER_IDENT &&
        ++net_actions > NET_MAX_ACTIONS_PER_POLL) {
      if (!net_action_cap_logged) {
        NET_LOG("net: rx action budget %d/poll hit - dropping flood\n",
                NET_MAX_ACTIONS_PER_POLL);
        net_action_cap_logged = true;
      }
      continue;
    }
    if (net_handle_ping_pong(h.msg_type, r)) continue;
    if (h.msg_type == Net::MSG_EVENT) {
      uint8_t code = r.u8();
      uint32_t arg = r.remaining() >= 4 ? r.u32() : 0;
      if (r.ok) {
        // Replay tee (client-side online recording) — same skip list as
        // the send tee in net_send_event. EV_GENERATION_START doubles as
        // the client's level-boundary checkpoint flush, mirroring the
        // host's flush at its generation rebuild.
        if (replay_ && code != Net::EV_PAUSE && code != Net::EV_RESUME &&
            code != Net::EV_BYE && code != Net::EV_ACHIEVEMENT) {
          replay_->record_event(code, arg);
          if (code == Net::EV_GENERATION_START) replay_->flush();
        }
        net_handle_event(code, arg);
      }
      continue;
    }
    if (h.msg_type == Net::MSG_SHOT) {
      // Host shot echo (PROTO 17), the mirror of the host's handler:
      // spawn an exact clone of the bullet the HOST player just fired
      // (same spawn, same spread-applied velocity) instead of waiting
      // for the next 10 Hz snapshot rebuild — which is where each host
      // bullet used to pop in, up to an interval late and already
      // down-range (obvious when spectating at the host's muzzle).
      // Leading by RTT/2 matches the rebuild's lead, so the clone and
      // its snapshot successor line up; the clone also plays the shot
      // sound, attenuated like every other remote cue.
      uint32_t id = r.u32();
      float sx = r.f32(), sy = r.f32(), svx = r.f32(), svy = r.f32();
      uint8_t flags = r.u8();
      if (!r.ok || players->empty()) continue;
      // PROTO 25: the header names the firing seat (the host's own player
      // today; another peer's relayed shot at B4). Never our own seat —
      // our shots are already local.
      Ship *host_ship = net_firer_ship(h.player_id);
      if (!host_ship) continue;
      float lead = net_lead_ms();
      if (std::isfinite(sx) && std::isfinite(sy) && std::isfinite(svx) &&
          std::isfinite(svy) && svx * svx + svy * svy < 25.0f) {
        host_ship->net_spawn_reported_bullet(
            id, Point(sx + svx * lead, sy + svy * lead), Point(svx, svy),
            (flags & 1) != 0, (flags & 2) != 0,
            (flags & 4) != 0);  // PROTO 18 pierce
        // Replay tee: the host gun's sound cue, at the un-led spawn point
        // (it is a sound position, not a pose).
        replay_record_shot(sx, sy, (flags & 4) ? 1 : 0);
      }
      continue;
    }
    if (h.msg_type == Net::MSG_LANCE) {
      // Host lance pulse echo (PROTO 18): display-only flash + sound on
      // the firing seat's ship; its kills arrive as ordinary removal
      // records.
      if (players->empty()) continue;
      Ship *host_ship = net_firer_ship(h.player_id);
      if (!host_ship) continue;
      size_t before = host_ship->lance_pulses.size();
      if (net_receive_lance_pulse(r, host_ship) &&
          host_ship->lance_pulses.size() > before)
        replay_record_polyline(Replay::FX_LANCE, host_ship,
                               host_ship->lance_pulses.back().points);
      continue;
    }
    if (h.msg_type == Net::MSG_SHOCK) {
      // Host shock bolt echo (PROTO 22): display-only exact segments + zap
      // sound on the firing seat's ship; kills replicate as removal/score
      // records.
      if (players->empty()) continue;
      Ship *shock_firer = net_firer_ship(h.player_id);
      if (!shock_firer) continue;
      std::vector<Point> pts;
      if (net_receive_shock_pulse(r, shock_firer, &pts))
        replay_record_polyline(Replay::FX_SHOCK, shock_firer, pts);
      continue;
    }
    if (h.msg_type == Net::MSG_BOUNCE) {
      // Authoritative ricochet (PROTO 19): the host sim bounced a bullet
      // we know by id — our own shot (already bounced here by the radial
      // approximation in net_cosmetic_impacts) or a host-shot clone
      // (which flew straight through). Snap it onto the real post-bounce
      // trajectory, leading by RTT/2 like the shot clones so it lines up
      // with the next snapshot rebuild. Unknown id = bullet already
      // expired or consumed locally — ignore.
      uint32_t id = r.u32();
      float bx = r.f32(), by = r.f32(), bvx = r.f32(), bvy = r.f32();
      uint8_t flags = r.u8();
      if (!r.ok || id == 0) continue;
      if (!(std::isfinite(bx) && std::isfinite(by) && std::isfinite(bvx) &&
            std::isfinite(bvy) && bvx * bvx + bvy * bvy < 25.0f)) continue;
      float lead = net_lead_ms();
      for (auto *gs : *players) {
        for (auto &b : gs->ship->bullets) {
          if (b.net_id != id) continue;
          b.position = WrappedPoint(bx + bvx * lead, by + bvy * lead);
          b.velocity = Point(bvx, bvy);
          b.set_net_flags(flags);
          break;
        }
      }
      continue;
    }
    if (h.msg_type == Net::MSG_PEER_IDENT) {
      // Seat identity relay (net_protocol.h): the host names the OTHER
      // clients' seats for the HUD rows. Our own seat and the host's are
      // skipped — this machine knows itself, and the host's identity came
      // through the WELCOME handshake + worker attestation. The name is
      // re-sanitized and the host's trust assertion clamped: an unknown
      // trust value from a newer build demotes to CLAIMED, so the strict
      // online context can only under-render it.
      uint8_t seat = r.u8();
      uint8_t platform = r.u8();
      uint8_t plat_trust = r.u8();
      uint8_t name_trust = r.u8();
      uint8_t name_len = r.u8();
      std::string raw_name;
      if (r.ok && name_len) {
        size_t n = name_len;
        if (n > r.remaining()) n = r.remaining();
        if (n > (size_t)NET_IDENTITY_NAME_MAX) n = NET_IDENTITY_NAME_MAX;
        const uint8_t *bytes = r.bytes(n);
        if (bytes) raw_name.assign((const char *)bytes, n);
      }
      // Trailing flags byte (appended post-PROTO-25-launch; see the send
      // side): absent from an older host's message, and absence means no
      // carve-out — the strict-context default.
      uint8_t flags = r.remaining() ? r.u8() : 0;
      if (!r.ok) continue;
      if (seat < 2 || seat > MAX_PLAYERS || (int)seat == net_local_seat())
        continue;
      NetIdentity id;
      id.platform = platform;
      id.platform_trust =
          plat_trust == NET_TRUST_ATTESTED ? NET_TRUST_ATTESTED
          : plat_trust ? NET_TRUST_CLAIMED : NET_TRUST_ABSENT;
      id.name = net_sanitize_name(raw_name);
      id.name_trust =
          id.name.empty() ? NET_TRUST_ABSENT
          : name_trust == NET_TRUST_ATTESTED ? NET_TRUST_ATTESTED
          : name_trust ? NET_TRUST_CLAIMED : NET_TRUST_ABSENT;
      net_seat_identities_[seat] = id;
      net_seat_offline_paired_[seat] = (flags & 1) != 0;
      NET_LOG("net: seat %u identity relay name='%s' platform=%u\n",
              (unsigned)seat, id.name.c_str(), (unsigned)platform);
      continue;
    }
    if (h.msg_type == Net::MSG_DELTA) {
      uint32_t snap_id = r.u32();  // monotonic with keyframes
      if (net_last_delta_id_ &&
          (int32_t)(snap_id - net_last_delta_id_) <= 0) {
        NET_LOG("net: DROPPED stale delta %u (last %u)\n", snap_id,
                net_last_delta_id_);
        continue;
      }
      net_last_delta_id_ = snap_id;
      size_t n = r.remaining();
      const uint8_t *body = r.bytes(n);
      if (!body) continue;
      std::vector<uint8_t> buf(body, body + n);
      // Replay tee (client-side online recording): every arriving delta,
      // in arrival order, BEFORE the stale gates below — those only guard
      // this machine's live timeline; playback consumes the stream fresh
      // at its own pace, where "stale" has no meaning.
      if (replay_) replay_->record_delta(buf);
      Save::MemStream in(buf);
      Save::GameState s;
      if (!Save::deserialize_game(in, s)) continue;
      if (!net_state_sane(s)) continue;
      // Drop stale backlog items (see net_host_est_): dyn records are
      // absolute, so a skipped delta spares the backward tour it would
      // have painted across every asteroid at once. But NEW/REMOVED
      // records are sent exactly once and are monotonic facts, so walk
      // the stream past the (poisonous) state/ship sections and apply
      // just the membership — otherwise a kill riding a gated delta
      // waited ~1 s for the keyframe backstop (late explosions).
      int behind = net_host_est_ >= 0 ? net_host_est_ - s.current_time : 0;
      if (behind > 120) {
        // Wedge hatch: a broken estimate shows as CONSTANT mild
        // staleness with nothing ever accepted (a real backlog drain
        // rushes toward fresh within a second) — after 30 straight
        // drops, take a mildly-stale item as the new anchor rather
        // than dead-reckoning forever.
        bool wedged = net_stale_streak_ >= 30 && behind < 1000;
        if (!wedged) {
          net_stale_streak_++;
          NET_LOG("net: dropped stale delta (%d ms behind), membership kept\n",
                  behind);
          if (net_apply_ship_extras(in, s, /*apply=*/false))
            net_apply_delta_asteroids(in, /*membership_only=*/true);
          else
            NET_LOG("net: stale-delta walk FAILED (stream mismatch)\n");
          continue;
        }
        NET_LOG("net: stale gate wedged (%d drops, %d ms behind) - re-anchoring\n",
                net_stale_streak_, behind);
      }
      net_stale_streak_ = 0;
      net_host_est_ = s.current_time;
      net_est_anchor_host_ = s.current_time;
      net_est_anchor_local_ = current_time;
      // The mini state carries everything but asteroids (its asteroid
      // vector is empty by construction, so the wholesale sections apply
      // exactly like a keyframe); asteroids follow as delta records.
      net_apply_state(s);
      if (!net_apply_ship_extras(in, s)) continue;
      net_apply_delta_asteroids(in);
      continue;
    }
    if (h.msg_type != Net::MSG_SNAPSHOT_CHUNK) continue;
    if (!net_assembler_->add_chunk(r)) continue;

    // Replay tee (client-side online recording): the reassembled keyframe,
    // exactly as the host built it — recorded before the sanity/stale
    // gates for the same reason as the delta tee above.
    if (replay_) replay_->record_keyframe(net_assembler_->payload());

    Save::MemStream in(net_assembler_->payload());
    Save::GameState s;
    if (!Save::deserialize_game(in, s)) continue;
    if (!net_state_sane(s)) continue;  // reject a hostile/corrupt snapshot
    // Same stale-backlog gate as deltas — a stale KEYFRAME tours the
    // whole world backward hardest of all. Bootstrap (est unset) always
    // applies; the next keyframe is at most a second away. Shares the
    // wedge hatch with the delta path (see above).
    if (net_ids_adopted_ && net_host_est_ >= 0 &&
        s.current_time + 120 < net_host_est_) {
      int behind = net_host_est_ - s.current_time;
      if (net_stale_streak_ < 30 || behind >= 1000) {
        net_stale_streak_++;
        NET_LOG("net: dropped stale keyframe (%d ms behind)\n", behind);
        continue;
      }
      NET_LOG("net: stale gate wedged (%d drops, %d ms behind) - re-anchoring\n",
              net_stale_streak_, behind);
    }
    net_stale_streak_ = 0;
    net_host_est_ = s.current_time;
    net_est_anchor_host_ = s.current_time;
    net_est_anchor_local_ = current_time;
    net_apply_state(s);
    net_apply_extras(in, s);
  }
}

// membership_only: the delta was stale-gated (see net_client_poll) — its
// dyn poses would tour the world backward, but its NEW and REMOVED records
// are monotonic facts (a breakup/kill that happened before the stall is
// still true now), so those still apply. Without this, a kill whose
// removal record rode a gated delta waited for the next 1 Hz keyframe —
// Glenn's "about a 1 second delay on ~1/20 asteroid hits".
void GLGame::net_apply_delta_asteroids(Save::Stream &in, bool membership_only) {
  // unordered_map + reserve: one bucket allocation and no tree balancing,
  // vs std::map's per-node red-black inserts, on this 10x/s lookup build.
  std::unordered_map<uint32_t, Asteroid *> by_id;
  by_id.reserve(objects->size() * 2);
  for (auto *a : *objects) by_id[a->net_id] = a;

  uint16_t n_new = 0;
  if (!nx_read(in, n_new)) return;
  if (n_new > 512) return;  // hostile/corrupt
  for (int i = 0; i < n_new; i++) {
    uint32_t id = 0;
    Save::Asteroid sa;
    if (!nx_read(in, id) || !Save::read_asteroid(in, sa)) return;
    if (by_id.find(id) != by_id.end()) continue;  // already known
    Asteroid *a = new Asteroid(sa.invincible, sa.invisible, sa.reflective,
                               sa.teleporting, sa.quantum, sa.tough,
                               sa.armoured, sa.phasing);
    a->restore_state(sa);
    a->net_id = id;
    objects->push_back(a);
    by_id[id] = a;
  }

  uint16_t n_dyn = 0;
  if (!nx_read(in, n_dyn)) return;
  if (n_dyn > 5000) return;
  // 1 Hz record-rate summary: at gen>=9 gravity dirties every asteroid
  // every delta, so this says how many hard authoritative writes the
  // asteroid field takes per second.
  if (Net::net_debug_enabled() && !membership_only) {
    static uint32_t s_last = 0;
    static int s_records = 0;
    s_records += n_dyn;
    uint32_t now = SDL_GetTicks();
    if (now - s_last >= 1000) {
      if (s_records) NET_LOG("net: delta dyn %d records/s\n", s_records);
      s_last = now;
      s_records = 0;
    }
  }
  for (int i = 0; i < n_dyn; i++) {
    uint32_t id = 0;
    float px, py, vx, vy;
    uint8_t health, state;
    if (!nx_read(in, id) || !nx_read(in, px) || !nx_read(in, py) ||
        !nx_read(in, vx) || !nx_read(in, vy) || !nx_read(in, health) ||
        !nx_read(in, state))
      return;
    if (membership_only) continue;  // stale pose — parse past it only
    std::unordered_map<uint32_t, Asteroid *>::iterator f = by_id.find(id);
    if (f == by_id.end()) continue;  // unknown id — next keyframe reconciles
    Asteroid *a = f->second;
    // Teleporting asteroid relocated on the host this instant (the
    // vulnerable window opens on arrival): play the warp locally.
    bool was_vulnerable = a->teleport_vulnerable;
    WrappedPoint old_render(a->position.x() + a->net_pose_err.x(),
                            a->position.y() + a->net_pose_err.y());
    a->position = WrappedPoint(px, py);
    a->velocity = Point(vx, vy);
    float rec_err = net_reconcile_pose(*a, old_render, /*sim_exact=*/true);
    // Who exactly diverges: id, hole distance, speed, special flags.
    if (Net::net_debug_enabled() && rec_err >= 30.0f) {
      float bh_dist = -1.0f;
      for (auto *bh : *black_holes) {
        Point p = bh->position.closest_to(a->position);
        float d = (p - a->position).magnitude();
        if (bh_dist < 0.0f || d < bh_dist) bh_dist = d;
      }
      NET_LOG("net: dyn err %.0f id=%u bh=%.0f spd=%.3f r=%.0f%s%s%s%s%s\n",
              rec_err, a->net_id, bh_dist, a->velocity.magnitude(), a->radius,
              a->reflective ? " refl" : "", a->teleporting ? " tele" : "",
              a->quantum ? " quant" : "", a->phasing ? " phase" : "",
              a->invincible ? " invinc" : "");
    }
    a->health = health;
    a->phased = (state & 1) != 0;
    a->teleport_vulnerable = (state & 2) != 0;
    a->teleport_pending = (state & 4) != 0;
    a->quantum_observed = (state & 8) != 0;
    if (!was_vulnerable && a->teleport_vulnerable) {
      // The delta carries only the flag, not the timer — without this
      // the client's own Asteroid::step sees vulnerable_time_left <= 0
      // on the very next step and snaps the indicator straight back to
      // the triangle. Mirror the host's window (glgame relocation sets
      // 5000); both clocks started within one snapshot interval, and
      // the host's clear ships as a state-byte delta anyway.
      a->vulnerable_time_left = 5000;
      a->explode();  // arrival debris, as the host's relocation shows
      if (warp_sound) Mix_PlayChannel(-1, warp_sound, 0);
      // A teleport must LOOK like a teleport: drop any render-continuity
      // offset so the rock appears at the arrival point this frame. The
      // 800-unit glide budget otherwise smeared short teleports across
      // the screen ("moved smoothly instead of jumping") — this flag
      // transition is the explicit signal, no distance guessing.
      a->net_pose_err = Point(0.0f, 0.0f);
    }
  }

  uint16_t n_rm = 0;
  if (!nx_read(in, n_rm)) return;
  if (n_rm > 5000) return;
  bool removed_any = false;
  for (int i = 0; i < n_rm; i++) {
    uint32_t id = 0;
    if (!nx_read(in, id)) return;
    std::unordered_map<uint32_t, Asteroid *>::iterator f = by_id.find(id);
    if (f == by_id.end()) continue;
    Asteroid *a = f->second;
    for (auto oi = objects->begin(); oi != objects->end(); ++oi) {
      if (*oi != a) continue;
      a->kill();
      // kill() is a no-op on invincible asteroids: an id the host dropped
      // is a stale ghost either way, and an ALIVE ghost in dead_objects
      // renders forever (with its score value on top). Only truly dead
      // ones stay for their debris.
      if (!a->is_alive() && !a->is_removable()) {
        // Real death: explosion audio (add_children is host-only).
        Asteroid::play_explode_sound(a->position);
        dead_objects->push_back(a);
      } else {
        delete a;
      }
      objects->erase(oi);
      removed_any = true;
      break;
    }
    by_id.erase(f);
  }
  // Invariant: no poll-path delete may leave the grid holding freed
  // pointers — later messages in the SAME drain probe it (respawn's
  // safe_position, spark events), which was a live use-after-free crash.
  if (removed_any) grid.update((std::list<Object *> *)objects);
  // Positive proof for the stale-delta walk: a misaligned parse would
  // silently misread these once-sent sections, so say when they land.
  if (membership_only && (n_new || n_rm))
    NET_LOG("net: stale delta membership applied (+%d -%d)\n",
            (int)n_new, (int)n_rm);
}

void GLGame::net_apply_state(const Save::GameState &s) {
  // The host's cheat keys must suppress THIS machine's unlocks too — the
  // flag rides every snapshot, so a mid-game skip-level arrives before
  // the rebuild it causes is evaluated below. NetClient only: WATCHING a
  // cheated replay must not poison the viewer's session flag.
  if (net_mode_ == NetClient && s.cheated) Achievements::note_cheat_used();

  // Generation rollover: the world grew — rebuild boundaries, grid and
  // starfield, drop every stale object (mirrors the host's rollover block;
  // the snapshot then repopulates everything below).
  const bool world_rebuilt = s.world_x != world.x() || s.world_y != world.y();
  net_world_rebuilt_last_apply_ = world_rebuilt;
  // Age the per-weapon primary-ammo anti-flicker latches once per apply. A
  // level rebuild clears every pickup and refills nothing, so any pending
  // latch is stale.
  for (int k = 1; k < NET_PRIMARY_KINDS; k++) {
    if (world_rebuilt) net_pickup_latch_[k] = 0;
    else if (net_pickup_latch_[k] > 0) net_pickup_latch_[k]--;
  }
  if (world_rebuilt) {
    // Level-clear achievements, mirroring the host's num_killable==0 block
    // (which never runs on a client): everything they need replicates —
    // the generation, our died_this_generation flag and weapons_fired_mask
    // (still PRE-apply here), and the cheat flag applied above. The
    // +1-generation gate keeps bootstrap/rejoin jumps from firing them.
    if (net_mode_ == NetClient && s.generation == generation + 1 &&
        !players->empty()) {
      Ship *me = players->back()->ship;  // local player is last on a client
      if (generation == 0) Achievements::unlock("clear_level1");
      Achievements::unlock("coop_clear");  // online is always two-player
      if (generation >= 8 && !me->died_this_generation)
        Achievements::unlock("no_damage_clear");
      // Black hole moved to level 14 (generation 13) — see the host block.
      if (generation >= 13 && !me->died_this_generation)
        Achievements::unlock("black_hole_survivor");
      Achievements::progress("reach_level15", (s.generation + 1) * 100 / 15);
      if (s.generation >= 9) {
        const uint32_t secondary_bits =
            (1u << (int)Save::WeaponEntry::Kind::Mine) |
            (1u << (int)Save::WeaponEntry::Kind::GigaMine) |
            (1u << (int)Save::WeaponEntry::Kind::Missile) |
            (1u << (int)Save::WeaponEntry::Kind::Shield) |
            (1u << (int)Save::WeaponEntry::Kind::Nova) |
            (1u << (int)Save::WeaponEntry::Kind::Turret);
        if ((me->weapons_fired_mask & secondary_bits) == 0)
          Achievements::unlock("no_secondary_level10");
      }
      // Counter-driven progress from the replicated (authoritative)
      // counters, once per level: covers paths that only credit
      // host-side (e.g. our missiles killing enemies in the host's sim).
      Achievements::progress("kills_1000", me->asteroid_kills / 10);
      Achievements::progress("enemies_10", me->enemy_kills * 10);
      Achievements::progress("score_3m", me->score / 30000);
    }
    world = Point(s.world_x, s.world_y);
    grid = Grid(world, Point(Asteroid::max_radius * 2, Asteroid::max_radius * 2));
    WrappedPoint::set_boundaries(world);
    delete starfield;
    starfield = new GLStarfield(world, star_density_scale());
    while (!objects->empty()) { delete objects->back(); objects->pop_back(); }
    while (!dead_objects->empty()) { delete dead_objects->back(); dead_objects->pop_back(); }
    Asteroid::num_killable = 0;
    // The rollover wiped every id — predicted-kill suppression entries
    // are meaningless against the rebuilt world.
    net_predicted_kills_.clear();
    // Local-ship bullets are client-owned (own_bullets): the rollover
    // wipe that used to arrive via the wholesale echo happens here now.
    // (Replay ghosts' bullets are record-owned — no wipe.)
    if (net_mode_ == NetClient && !players->empty())
      players->back()->ship->bullets.clear();
  }

  generation = s.generation;
  level_cleared = s.level_cleared;
  time_until_next_generation = s.time_until_next_generation;
  current_time = s.current_time;

  // Time-slow effect (PROTO 24): adopt the host's countdown and re-assert
  // the collector's rotation comp from the owner index, so this machine's
  // extrapolation loop slows its step scheduling in lockstep with the
  // host's sim (and a replay shows the recorded slow motion the same way).
  // Clamped to the legal window: a hostile snapshot could otherwise slow
  // this machine indefinitely.
  {
    int ms = s.time_slow_ms_left;
    if (ms < 0) ms = 0;
    if (ms > kTimeSlowWallMs / kTimeSlowFactor)
      ms = kTimeSlowWallMs / kTimeSlowFactor;
    if ((ms > 0) != time_slow_active())
      NET_LOG("net: time slow %s (%d ms)\n", ms > 0 ? "adopted" : "ended", ms);
    // Audio edges. A fresh window (or a mid-effect REFILL, which never
    // flips the active bit) plays the engage dive; the >1000 ms floors keep
    // the boundary race quiet — after a local countdown end, one more apply
    // can still carry a ~100 ms residue, which must not re-cue. The end
    // sweep is refractory-deduped against time_slow_step's.
    {
      int prev = time_slow_ms_left_;
      bool was = time_slow_active();
      if (ms > 0 && ((!was && ms > 1000) || (was && ms > prev + 1000)))
        time_slow_cue(true);
      if (ms == 0 && was) time_slow_cue(false);
    }
    time_slow_ms_left_ = ms;
    time_slow_ship_ = NULL;
    uint8_t idx = 0;
    for (auto *gs : *players) {
      bool owner = ms > 0 && idx == s.time_slow_player;
      gs->ship->time_slow_rotation_comp = owner ? (float)kTimeSlowFactor : 1.0f;
      if (owner) time_slow_ship_ = gs->ship;
      idx++;
    }
  }

  // The grid still holds raw pointers from the last step-loop rebuild,
  // but this poll drain may already have DELETED asteroids (removal
  // records, stale-membership kills, keyframe leftovers) in an earlier
  // message. The player restore below probes the grid on every apply
  // (restore_state -> respawn -> safe_position), and walking those
  // dangling pointers was a real crash (mac: Object::collide on freed
  // memory under net_apply_state). Rebuild from the live list first.
  grid.update((std::list<Object *> *)objects);

  // NetReplay: a mid-run player-2 join lives in the records — grow the
  // ghost roster when a snapshot carries more players than we have (the
  // player loop below silently ignores snapshot players past the list;
  // netplay never needed growth because both players exist from the
  // lobby). Mirrors add_remote_player minus the host-side arming: no
  // bindings, no shot reporting — the records own its pose and bullets.
  // The ghost starts dead; the extras' alive-transition respawns it at
  // the recorded join position, and split-screen engages via
  // players->size() the moment it lands.
  if (net_mode_ == NetReplay) {
    while (players->size() < s.players.size() &&
           (int)players->size() < MAX_PLAYERS) {
      GLShip *ghost = make_seat_ship(grid, (int)players->size());
      ghost->ship->set_missile_asteroids((std::list<Object *> *)objects);
      ship_objects->push_back(ghost->ship);
      for (auto *p : *players) p->ship->set_missile_ships(ship_objects);
      ghost->ship->set_missile_ships(ship_objects);
      ghost->ship->missiles_seek_players = friendly_fire;
      ghost->ship->set_black_holes(black_holes);
      ghost->ship->net_remote_gun = true;
      players->push_back(ghost);
      SDL_Log("replay: player %d joined", (int)players->size());
    }
  }

  // Players: the remote host ship snaps to the snapshot; the local ship
  // takes stats/weapons but blends its predicted pose toward the host's
  // authoritative one (~0.35 per snapshot) so corrections don't jerk.
  // NetReplay: there is no local ship — EVERY ship takes the remote snap
  // path (is_local false), so all ghosts ride the records exactly.
  // PROTO 25: a v19+ entry names its seat — resolve the ship BY seat; a
  // pre-v19 stream (seat 0, old replay files) keeps the positional walk.
  // Both are the same pairing while seats are dense (entry i = seat i+1).
  auto it = players->begin();
  for (size_t i = 0; i < s.players.size() && it != players->end(); i++, ++it) {
    GLShip *gs_rec = *it;
    if (s.players[i].seat) {
      GLShip *by_seat = player_by_seat(s.players[i].seat);
      if (!by_seat) continue;  // that seat has no local ship (yet)
      gs_rec = by_seat;
    }
    Ship *ship = gs_rec->ship;
    bool is_local = net_mode_ == NetClient && (gs_rec == players->back());

    // Mid-respawn on the host: skip the full restore — restore_state runs
    // respawn(), which would resurrect the corpse (burning a life) every
    // snapshot until the host's countdown ends. Track the HUD scalars; the
    // extras' alive-transition brings the ship back for real.
    if (s.players[i].respawning) {
      ship->score = s.players[i].score;
      ship->lives = s.players[i].lives;
      ship->kills = s.players[i].kills;
      continue;
    }

    WrappedPoint old_pos = ship->position;
    Point old_vel = ship->velocity;
    Point old_facing = ship->facing;
    bool was_alive = ship->is_alive();
    // restore_state runs respawn()/reset(), which clears every held input
    // flag — and keys only re-assert them on the next key-DOWN event. The
    // whole held state must be captured and re-applied for the local ship,
    // or held keys die ~100 ms after each press (10 Hz snapshots).
    bool held_left = ship->rotation_direction == Ship::LEFT;
    bool held_right = ship->rotation_direction == Ship::RIGHT;
    bool held_thrust = ship->thrusting;
    bool held_reverse = ship->reversing;
    bool armed_shoot = !ship->primary_weapons.empty() &&
                       (*ship->primary)->is_shooting();
    // Fire cooldown of the selected primary: the restore rebuilds the
    // weapon list with fresh objects, and a fresh weapon fires the instant
    // it is re-armed — an extra shot (and shoot sound) every snapshot
    // while the trigger is held.
    int shot_cooldown = 0;
    int shot_burst_pending = 0;  // burst shots still owed by the current pull
    int shock_cooldown = -1;  // -1: the selected primary is not a Shock
    if (!ship->primary_weapons.empty()) {
      Weapon::Default *dw = dynamic_cast<Weapon::Default *>(*ship->primary);
      if (dw) {
        shot_cooldown = dw->cooldown();
        shot_burst_pending = dw->burst_pending();
      }
      Weapon::Shock *sw = dynamic_cast<Weapon::Shock *>(*ship->primary);
      if (sw) shock_cooldown = sw->get_cooldown();
    }
    bool armed_secondary = ship->secondary != ship->secondary_weapons.end() &&
                           (*ship->secondary)->is_shooting();
    float analog_rot = ship->rotation_scale;
    float analog_thrust = ship->thrust_analog;
    float analog_reverse = ship->reverse_analog;
    // Local bullets are client-owned (own_bullets): restore_state's
    // reset() clears the list, and with the echo rebuild gone nothing
    // repopulated it — every in-flight shot vanished on the next apply
    // ("only rendering for one tick"). Carry them across the restore.
    std::vector<Particle> kept_bullets;
    if (is_local) kept_bullets.swap(ship->bullets);

    // Anti-flicker: remember the local ship's live primary ammo before the
    // restore rebuilds the weapon list from the (lagging) snapshot, so a
    // snapshot that hasn't yet seen our just-fired shots can't bump the count
    // back up. Keyed by weapon name — one of each primary kind at most.
    std::map<std::string, int> pre_primary_ammo;
    if (is_local)
      for (auto *w : ship->primary_weapons)
        pre_primary_ammo[w->name()] = w->ammo();

    ship->restore_state(s.players[i], grid);
    if (is_local) ship->bullets.swap(kept_bullets);
    // restore_state -> respawn() -> safe_position() relocates the ship to a
    // RANDOM spot whenever the restored position is within 50 units of an
    // asteroid. Right for loading a solo save into a freshly-built world;
    // wrong 10x/s against live snapshots — the host's position is the truth
    // however close to a rock it is. Pin the authoritative position back.
    ship->position = WrappedPoint(s.players[i].pos_x, s.players[i].pos_y);

    // The remote (host) ship is client-extrapolated between applies like
    // every other world object — reconcile instead of overwriting, or it
    // shimmers with the channel's delivery jitter exactly like the
    // asteroids did. Velocity stays authoritative; facing is reconciled the
    // same way as position (see net_reconcile_facing) — the value is still
    // authority's, only the drawn approach to it is smoothed.
    if (!is_local && was_alive && ship->is_alive() && !world_rebuilt) {
      net_reconcile_pose(*ship, old_pos, /*sim_exact=*/false);
      net_reconcile_facing(*ship, old_facing);
    }

    if (is_local && was_alive && ship->is_alive() && !world_rebuilt) {
      // v12: this machine's pose is AUTHORITATIVE — the host adopts it
      // from every INPUT — so the snapshot's copy is just our own report
      // echoed back a round trip late. Ignore it outright: the pilot is
      // never corrected, which is what finally killed the relay
      // rubberbanding (every blackout used to land here as a huge
      // correction that slewed the camera and, with it, the whole world).
      // Host-driven respawns/teleports still land via the explicit
      // warp-count snap in the extras; death via the alive transition.
      ship->position = old_pos;
      ship->velocity = old_vel;
      ship->facing = old_facing;
      ship->net_pose_err = Point(0.0f, 0.0f);
      ship->net_facing_err = 0.0f;  // nothing owed: this facing IS authority

      ship->rotate_left(held_left);
      ship->rotate_right(held_right);
      ship->thrust(held_thrust);
      ship->reverse(held_reverse);
      // Restore the preserved Shock cooldown before re-arming: a fresh Shock
      // (cooldown 0) fires on its first armed step(), so re-arming below with
      // a zeroed cooldown would emit an extra bolt+sound every snapshot while
      // the trigger is held. (Shock::shoot() no longer fires directly — the
      // semi-auto rewrite moved firing into step() — but the ordering still
      // matters for that first post-restore step.)
      if (shock_cooldown >= 0 && !ship->primary_weapons.empty()) {
        Weapon::Shock *sw = dynamic_cast<Weapon::Shock *>(*ship->primary);
        if (sw) sw->set_cooldown(shock_cooldown);
      }
      ship->shoot(armed_shoot);
      ship->fire_secondary(armed_secondary);
      if ((shot_cooldown > 0 || shot_burst_pending > 0) &&
          !ship->primary_weapons.empty()) {
        Weapon::Default *dw = dynamic_cast<Weapon::Default *>(*ship->primary);
        if (dw) {
          if (shot_cooldown > 0) dw->set_cooldown(shot_cooldown);
          dw->set_burst_pending(shot_burst_pending);
        }
      }
      ship->rotation_scale = analog_rot;
      ship->thrust_analog = analog_thrust;
      ship->reverse_analog = analog_reverse;

      // Anti-flicker: the restore just overwrote our limited-ammo primaries
      // with the snapshot's copy. If that copy is HIGHER than what we held a
      // moment ago, the host simply hasn't seen our latest shots yet — clamp
      // it back down so the counter doesn't blip up for a round trip. A
      // genuine pickup is exempted by THAT WEAPON's EV_PICKUP latch (keyed by
      // kind and consumed on its rise — a shared latch let a blip on one
      // weapon steal the exemption meant for another's real pickup, pinning
      // the real gain low every apply thereafter). Lower-or-equal snapshots
      // are the host's authoritative decrements and pass through untouched.
      for (auto *w : ship->primary_weapons) {
        auto pi = pre_primary_ammo.find(w->name());
        if (pi == pre_primary_ammo.end()) continue;
        int pre = pi->second, snap = w->ammo();
        if (snap > pre) {
          int kind = NET_PRIM_NONE;
          if (dynamic_cast<Weapon::Beam *>(w)) kind = NET_PRIM_BEAM;
          else if (dynamic_cast<Weapon::Lance *>(w)) kind = NET_PRIM_LANCE;
          else if (dynamic_cast<Weapon::Shock *>(w)) kind = NET_PRIM_SHOCK;
          else if (dynamic_cast<Weapon::GodMode *>(w)) kind = NET_PRIM_GOD;
          if (kind != NET_PRIM_NONE && net_pickup_latch_[kind] > 0)
            net_pickup_latch_[kind] = 0;  // this weapon's real pickup: accept
          else
            w->set_ammo(pre);        // lag blip: suppress the upward flicker
        }
      }
    } else if (is_local && world_rebuilt) {
      // Level transition: keep the authoritative new-level pose and do NOT
      // re-apply the held state — the host cleared the remote ship's
      // controls (and suppresses our held INPUT bits until re-press), so
      // the local prediction starts the level with controls released too,
      // exactly like the host's own player.
      gs_rec->net_shoot_held = false;
      gs_rec->net_secondary_held = false;
    }
  }

  // Pickups: the set changes only when one drops or is collected (seconds
  // apart), but this applies 10x/s. Pickups are stationary and the host
  // sends them in a stable order, so a matching count with matching
  // positions means the set is unchanged — skip the delete/new storm. Any
  // divergence (a type swap at the same spot, astronomically unlikely) is
  // reconciled by the next 1 Hz keyframe.
  bool pickups_same = pickups->size() == s.pickups.size();
  if (pickups_same) {
    size_t i = 0;
    for (auto *p : *pickups) {
      const Save::Pickup &sp = s.pickups[i++];
      if (fabsf(p->position.x() - sp.pos_x) > 0.5f ||
          fabsf(p->position.y() - sp.pos_y) > 0.5f) { pickups_same = false; break; }
    }
  }
  if (!pickups_same) {
    while (!pickups->empty()) { delete pickups->back(); pickups->pop_back(); }
    // make_pickup: the single shared PickupType switch — this rebuild used to
    // be a hand-maintained copy that silently missed every new pickup type.
    for (const auto &sp : s.pickups) {
      Pickup *p = make_pickup(sp);
      if (p) pickups->push_back(p);
    }
  }

  // Black holes: cheap wholesale rebuild (0 or 1 in practice).
  while (!black_holes->empty()) { delete black_holes->back(); black_holes->pop_back(); }
  for (const auto &sbh : s.black_holes)
    black_holes->push_back(new BlackHole(WrappedPoint(sbh.pos_x, sbh.pos_y)));

  // Mid-game hazards (pulsar/comet/seeker): host-authoritative and MOVING
  // (comet drifts, seeker homes), so a wholesale rebuild each snapshot would
  // teleport them 10x/s. They carry no net id, so match each snapshot hazard
  // to the nearest live replica of the same kind and reconcile it in place —
  // this keeps the comet trail / seeker debris continuous and survives a
  // mid-list death (a rebuild-by-index would misalign after it). The client
  // extrapolates their motion between applies in tick_net_client; kills stay
  // host-side (the lethal pulsar wave, comet/seeker rams) and land as the
  // replica dropping out of the snapshot below.
  {
    std::vector<Hazard *> live(hazards->begin(), hazards->end());
    std::vector<bool> used(live.size(), false);
    hazards->clear();
    for (const auto &sh : s.hazards) {
      int best = -1;
      float best_d2 = 1e30f;
      for (size_t j = 0; j < live.size(); j++) {
        if (used[j] || !live[j]->is_alive()) continue;
        if ((int)live[j]->kind_of() != (int)sh.kind) continue;
        Point c = live[j]->position.closest_to(Point(sh.pos_x, sh.pos_y));
        float dx = c.x() - sh.pos_x, dy = c.y() - sh.pos_y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) { best_d2 = d2; best = (int)j; }
      }
      if (best >= 0) {
        live[best]->apply_net_state(sh);
        used[best] = true;
        hazards->push_back(live[best]);
      } else {
        hazards->push_back(Hazard::from_state(sh, world));
        NET_LOG("net: hazard replica spawned (kind %d)\n", (int)sh.kind);
      }
    }
    // Live replicas the host no longer reports: an alive one just died there
    // (play its death burst); a fading one keeps its debris. Either way keep
    // it until is_removable(), so the burst is seen before it is reaped.
    for (size_t j = 0; j < live.size(); j++) {
      if (used[j]) continue;
      if (live[j]->is_alive()) {
        live[j]->destroy();
        // The host plays explode_sound when a hazard breaks up; replicate it
        // at the wreck, gated by the local camera's distance (net_listener).
        float vol = net_listener_volume(
            Point(live[j]->position.x(), live[j]->position.y()));
        if (vol > 0.0f && Asteroid::explode_sound != NULL) {
          Mix_VolumeChunk(Asteroid::explode_sound, (int)(MIX_MAX_VOLUME * vol));
          Mix_PlayChannel(-1, Asteroid::explode_sound, 0);
        }
        NET_LOG("net: hazard replica destroyed (kind %d)\n",
                (int)live[j]->kind_of());
      }
      if (live[j]->is_removable()) delete live[j];
      else hazards->push_back(live[j]);
    }
  }

  // Stations restore in place; created/destroyed on presence transitions.
  // restore_state reconciles its deployed enemies in place (no clearing
  // here — recreating every GLEnemy at 10 Hz re-uploaded meshes and held
  // the client near 15 fps at gen 20+; see glstation.cpp).
  if (s.station.present) {
    if (!station)
      // Client replica: its enemies never steer or shoot (the host owns
      // that), so the lead value is cosmetic here — passed for symmetry.
      station = new GLStation(grid, enemies, players, (std::list<Object *> *)objects,
                              hostile_aim_lead(generation));
    station->restore_state(s.station, grid);
  } else if (station) {
    while (!enemies->empty()) { delete enemies->back(); enemies->pop_back(); }
    delete station;
    station = NULL;
  }
  // Keep the replica for the record's whole `present` window, not just
  // while alive: the host keeps its dead mini around while the debris
  // burst fades, and deleting the replica on the alive->dead delta
  // skipped the explosion entirely on the client (Glenn's report). The
  // transition runs destroy() BEFORE the restore overwrites `alive` —
  // destroy() no-ops on an already-dead object — so the client spawns
  // the same local debris burst the host is showing.
  if (s.mini_station.present) {
    // Transition detection wants a replica we watched die — a fresh one
    // (bootstrap mid-fade) starts dead with no burst.
    bool watched_alive = mini_station != NULL && mini_station->is_alive();
    if (!mini_station)
      mini_station = new GLMiniStation(grid, players, (std::list<Object *> *)objects,
                                       hostile_aim_lead(generation));
    if (watched_alive && !s.mini_station.alive) {
      // Burst at the authoritative death spot, not the extrapolated one.
      mini_station->position =
          WrappedPoint(s.mini_station.pos_x, s.mini_station.pos_y);
      mini_station->destroy();
      NET_LOG("net: mini-station death burst (replica destroyed locally)\n");
    }
    mini_station->restore_state(s.mini_station);
  } else if (mini_station) {
    delete mini_station;
    mini_station = NULL;
  }
}

void GLGame::net_apply_extras(Save::Stream &in, const Save::GameState &s,
                              uint16_t ver) {
  if (!net_apply_ship_extras(in, s, true, ver)) return;
  net_apply_keyframe_asteroid_ids(in, s);
}

bool GLGame::net_apply_ship_extras(Save::Stream &in, const Save::GameState &s,
                                   bool apply, uint16_t ver) {
  uint32_t nplayers = 0;
  if (!nx_read(in, nplayers)) return false;
  // PROTO 25: each record leads with its seat byte — but only when the
  // GameState itself carries stamped seats (v19+; always true on the live
  // wire, absent in pre-v19 replay files, which keep the positional walk).
  bool seat_led = !s.players.empty() && s.players[0].seat != 0;
  auto it = players->begin();
  for (uint32_t i = 0; i < nplayers; i++) {
    uint8_t rec_seat = 0;
    if (seat_led && (!nx_read(in, rec_seat) || rec_seat == 0 ||
                     rec_seat > MAX_PLAYERS))
      return false;
    NetShipExtras ex;
    if (!nx_read(in, ex.alive) || !nx_read(in, ex.temperature) ||
        !nx_read(in, ex.time_until_respawn) || !nx_read(in, ex.time_left_invincible) ||
        !nx_read(in, ex.god_ms) || !nx_read(in, ex.shield) ||
        !nx_read(in, ex.warp_count) || !nx_read(in, ex.move_flags))
      return false;
    if (!apply) {
      // Parse-only walk (stale delta): every pose/effect in this section
      // is stale poison — only the stream position matters, so the
      // asteroid membership records behind it can still be reached.
      if (!nx_read_projectiles(in, NULL, false, 0.0f, false, ver >= 20))
        return false;
      continue;
    }
    GLShip *gs_rec;
    if (seat_led) {
      gs_rec = player_by_seat(rec_seat);
      if (!gs_rec) return false;
    } else {
      if (it == players->end()) return false;
      gs_rec = *it;
    }
    Ship *ship = gs_rec->ship;

    // The host moved this ship discontinuously (respawn, teleport, new-level
    // spawn) since the last snapshot: the pose is absolute. For the local
    // ship that overrides net_apply_state's prediction blend, which would
    // otherwise slide the ship across the world to the new position.
    // (NetClient only — replay ghosts snap in net_apply_state already.)
    bool local_ship = net_mode_ == NetClient &&
                      (seat_led ? (int)rec_seat == net_local_seat()
                                : gs_rec == players->back());
    if (local_ship && i < s.players.size()) {
      if (net_have_warp_ && ex.warp_count != net_prev_warp_ && ex.alive) {
        ship->position = WrappedPoint(s.players[i].pos_x, s.players[i].pos_y);
        ship->velocity = Point(s.players[i].vel_x, s.players[i].vel_y);
        ship->facing = Point(s.players[i].facing_x, s.players[i].facing_y);
        NET_LOG("net: warp snap (count %u)\n", (unsigned)ex.warp_count);
      }
      net_prev_warp_ = ex.warp_count;
      net_have_warp_ = true;
    }

    if (!ex.alive && ship->is_alive()) {
      // Host says this ship died: explode locally too — except during the
      // replay bootstrap, where dead-in-the-spawn-countdown is initial
      // state, not a death. The restore's respawn() resurrected the ship
      // (the resume-a-save UX), so this "transition" ran the full death
      // theatre — kill()'s hull-debris explode(), the boom sound AND the
      // detonate flash — before a recorded first countdown that never had
      // one. quiet_unspawn puts it back dead with none of that, and the
      // life the resurrect burned comes back from the recorded scalars.
      if (replay_bootstrap_apply_) {
        ship->quiet_unspawn();
        if (i < s.players.size()) ship->lives = s.players[i].lives;
      } else {
        ship->kill_stop();
        ship->detonate();
      }
    } else if (ex.alive && !ship->is_alive() && i < s.players.size()) {
      // Host respawned it: bring it back at the authoritative position.
      ship->respawn(grid, false);
      ship->position = WrappedPoint(s.players[i].pos_x, s.players[i].pos_y);
      ship->velocity = Point(s.players[i].vel_x, s.players[i].vel_y);
      // respawn() fired its spawn flash at an interim random position
      // (the authoritative one is only pinned above, after the fact);
      // re-fire it where the ship actually spawned.
      ship->bullets.clear();
      ship->detonate();
    }
    ship->temperature = ex.temperature;
    ship->time_until_respawn = ex.time_until_respawn;
    ship->time_left_invincible = ex.time_left_invincible;
    // restore_state -> respawn() force-sets invincible=true (and restarts
    // the shield hum) every snapshot; reflect the authoritative state
    // instead or the shield ring flickers and the hum plays constantly.
    ship->invincible = ex.time_left_invincible > 0 || ex.god_ms > 0 ||
                       ex.shield != 0;
    // The hum is a personal cue: only the LOCAL ship (last in the list on
    // the client) gets it. The remote host's ship respawning far away
    // otherwise plays short full-volume hums that sound random. A replay
    // has NO local ship: every ghost takes the remote treatment — its
    // bullets and exhaust flags come from the records (the local-ship
    // variant deliberately skips both as client-owned) — but every ghost
    // IS a watched ship (its own viewport in 2P), and the recorded offline
    // game hummed for every local player, so the hum keys off the
    // replicated invincibility for all of them. This is also the ONLY hum
    // source in playback: quiet restores suppress respawn()'s.
    // (local_ship computed at the top of the loop — by seat on a v19+
    // stream, by list position on a legacy one.)
    bool hum_ship = net_mode_ == NetReplay ? true : local_ship;
    ship->set_shield_hum(hum_ship && ship->is_alive() && ship->invincible &&
                         ex.god_ms <= 0);
    // Movement flags drive the remote ship's exhaust-trail emitters (the
    // restore cleared them). Raw flag writes: Ship::thrust()/reverse()
    // touch the shared boost-sound volume, which belongs to local input.
    // The local ship keeps its own input-driven flags.
    if (!local_ship) {
      ship->thrusting = (ex.move_flags & 1) != 0;
      ship->reversing = (ex.move_flags & 2) != 0;
      int rot = (ex.move_flags >> 2) & 3;
      ship->rotation_direction = rot == 1   ? Ship::LEFT
                                 : rot == 2 ? Ship::RIGHT
                                            : Ship::NONE;
      // This flag is a 10 Hz guess about a key that is still held, so turn
      // slower than the real ship and let each snapshot's reconcile make up
      // the difference forwards — see Ship::net_rotation_damp. Applies to
      // replay ghosts for the same reason: the records are the same 10 Hz
      // cadence.
      ship->net_rotation_damp = NET_ROTATION_DAMP;
    }
    // god-mode / shield presentation on the client is a Milestone-1 cut
    // (both still function — the host simulates them; only their local
    // visual/audio flourishes are missing).

    if (!nx_read_projectiles(in, ship, net_world_rebuilt_last_apply_,
                             net_lead_ms(), /*own_bullets=*/local_ship,
                             /*has_turrets=*/ver >= 20))
      return false;
    if (!seat_led) ++it;
  }

  // v6: mini-station bullets (see nx_write_mini_station_bullets). The
  // state apply ran first, so mini_station reflects host presence.
  uint16_t n_ms = 0;
  if (!nx_read(in, n_ms)) return false;
  if (n_ms > 512) return false;  // hostile/corrupt
  std::vector<Particle> ms_bullets;
  for (int i = 0; i < n_ms; i++) {
    float x, y, vx, vy;
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) ||
        !nx_read(in, vy))
      return false;
    ms_bullets.push_back(Particle(
        Point(x + vx * net_lead_ms(), y + vy * net_lead_ms()),
        Point(vx, vy), 2000.0f));
  }
  if (apply && mini_station) mini_station->bullets.swap(ms_bullets);

  // v7: station enemies' bullets, in the same order the station restore
  // just rebuilt the enemies list.
  uint16_t n_en = 0;
  if (!nx_read(in, n_en)) return false;
  if (n_en > 256) return false;  // hostile/corrupt
  auto ei = enemies->begin();
  for (int e = 0; e < n_en; e++) {
    uint32_t enemy_id = 0;
    if (!nx_read(in, enemy_id)) return false;
    uint16_t nb = 0;
    if (!nx_read(in, nb)) return false;
    if (nb > 512) return false;
    std::vector<Particle> ebs;
    for (int i = 0; i < nb; i++) {
      float x, y, vx, vy;
      if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) ||
          !nx_read(in, vy))
        return false;
      ebs.push_back(Particle(
          Point(x + vx * net_lead_ms(), y + vy * net_lead_ms()),
          Point(vx, vy), 2000.0f));
    }
    if (apply && ei != enemies->end()) {
      // One-shot field check that real ids are riding the wire — an
      // all-zero stream means the host-side mint regressed (it lives in
      // GLEnemy's ctor, NOT the unused Enemy class) and exact claims
      // would silently collapse onto the first alive enemy.
      if (Net::net_debug_enabled() && enemy_id != 0) {
        static bool s_ids_seen = false;
        if (!s_ids_seen) {
          s_ids_seen = true;
          NET_LOG("net: enemy wire ids live (first=%u)\n", enemy_id);
        }
      }
      // PROTO 16: re-stamp identity on the freshly-rebuilt replica, and
      // keep locally-killed enemies dead — the station restore just
      // resurrected everything, and the host's authoritative state only
      // agrees once the claim lands (~RTT/2). Entries expire in 3 s.
      (*ei)->ship->net_ship_id = enemy_id;
      std::map<uint32_t, int>::iterator pk =
          net_predicted_ship_kills_.find(enemy_id);
      if (pk != net_predicted_ship_kills_.end() && current_time < pk->second)
        (*ei)->ship->alive = false;
      (*ei)->ship->bullets.swap(ebs);
      ++ei;
    }
  }
  return true;
}

void GLGame::net_apply_keyframe_asteroid_ids(Save::Stream &in,
                                             const Save::GameState &s) {
  // Asteroids: match by net_id; new ids construct + restore, missing ids
  // play the standard death visual so host-side kills still explode here.
  uint32_t n_ids = 0;
  if (!nx_read(in, n_ids)) return;

  // Bootstrap: the constructor just built these asteroids from this very
  // snapshot, in the same order as this id array — adopt the host ids in
  // place. The old path replaced every asteroid and killed the originals,
  // which flashed their score values (debris) for a second on every join.
  if (!net_ids_adopted_) {
    net_ids_adopted_ = true;
    if (n_ids == objects->size() && n_ids == s.asteroids.size()) {
      size_t i = 0;
      std::vector<uint32_t> ids_tmp(n_ids);
      for (uint32_t k = 0; k < n_ids; k++)
        if (!nx_read(in, ids_tmp[k])) return;
      for (auto *a : *objects) a->net_id = ids_tmp[i++];
      NET_LOG("net: bootstrap adopted %u asteroid ids\n", n_ids);
      return;
    }
  }
  // Check the count BEFORE allocating: s.asteroids is already bounded by
  // the deserializer, so a hostile n_ids can't force a huge vector here.
  if (n_ids != s.asteroids.size()) return;  // malformed snapshot
  std::vector<uint32_t> ids(n_ids);
  for (uint32_t i = 0; i < n_ids; i++)
    if (!nx_read(in, ids[i])) return;

  std::unordered_map<uint32_t, Asteroid *> by_id;
  by_id.reserve(objects->size() * 2);
  for (auto *a : *objects) by_id[a->net_id] = a;

  for (uint32_t i = 0; i < n_ids; i++) {
    const Save::Asteroid &sa = s.asteroids[i];
    std::unordered_map<uint32_t, Asteroid *>::iterator found = by_id.find(ids[i]);
    if (found != by_id.end()) {
      Asteroid *a = found->second;
      WrappedPoint old_render(a->position.x() + a->net_pose_err.x(),
                              a->position.y() + a->net_pose_err.y());
      // Spin stays CLIENT-continuous: the restored angle is RTT/2 stale,
      // so adopting it twitched every rock's rotation backwards once per
      // second — "constant jitter" that no position diagnostic saw. Both
      // sides advance at the same rotation_speed (which IS adopted), so
      // the local angle never drifts; same for the armoured weak-spot
      // angle.
      float spin = a->rotation;
      float armour = a->armour_angle;
      a->restore_state(sa);
      a->rotation = spin;
      a->armour_angle = armour;
      net_reconcile_pose(*a, old_render, /*sim_exact=*/true);
      by_id.erase(found);
    } else {
      // Predicted-kill suppression (PROTO 13): we killed this id locally
      // on our own hit; a keyframe cut before the host processes the
      // claim still lists it, and re-creating it would resurrect the
      // rock for ~RTT. The entry expires in 3 s regardless.
      std::map<uint32_t, int>::iterator pk =
          net_predicted_kills_.find(ids[i]);
      if (pk != net_predicted_kills_.end() && current_time < pk->second)
        continue;
      Asteroid *a = new Asteroid(sa.invincible, sa.invisible, sa.reflective,
                                 sa.teleporting, sa.quantum, sa.tough,
                                 sa.armoured, sa.phasing);
      a->restore_state(sa);
      a->net_id = ids[i];
      objects->push_back(a);
    }
  }

  // Anything left in the map no longer exists on the host — kill it here
  // for the debris, then let dead_objects fade it out.
  if (!by_id.empty()) {
    auto oi = objects->begin();
    while (oi != objects->end()) {
      if (by_id.find((*oi)->net_id) == by_id.end() || by_id[(*oi)->net_id] != *oi) {
        ++oi;
        continue;
      }
      (*oi)->kill();
      // See the delta removal path: invincible asteroids survive kill();
      // an alive ghost must not linger in dead_objects.
      if (!(*oi)->is_alive() && !(*oi)->is_removable()) {
        // A real host-side death: the sound lives in add_children(),
        // which only the host runs — play it with the debris here.
        Asteroid::play_explode_sound((*oi)->position);
        dead_objects->push_back(*oi);
      } else {
        delete *oi;
      }
      oi = objects->erase(oi);
    }
  }

  grid.update((std::list<Object *> *)objects);
}

void GLGame::focus_gained() {
  // M3-1: returning to a disconnected client (mobile resume) rejoins at
  // once instead of waiting out the countdown.
  if (net_mode_ == NetClient && net_any_peer_lost() &&
      net_client_rejoin_ms_ > 300)
    net_client_rejoin_ms_ = 300;
  Mix_ResumeMusic();
  if(auto_paused) {
    toggle_pause();
    auto_paused = false;
  } else if(pause_music_channel >= 0) {
    // Manually paused before the focus loss: bring the pause tune back.
    Mix_Resume(pause_music_channel);
  }
}

bool GLGame::cleared() const {
  return level_cleared;
}

// Adds the scope's wall time to an accumulator + running max on exit —
// early returns in tick() are common, so bracketing must be RAII.
namespace {
struct PerfScope {
  Uint32 t0, *acc, *mx;
  PerfScope(Uint32 *a, Uint32 *m) : t0(SDL_GetTicks()), acc(a), mx(m) {}
  ~PerfScope() { Uint32 d = SDL_GetTicks() - t0; *acc += d; if (d > *mx) *mx = d; }
};
}  // namespace

void GLGame::perf_report() {
  Uint32 now = SDL_GetTicks();
  if (perf_window_start_ == 0) perf_window_start_ = now;
  perf_frames_++;
  // A frame gap no real frame produces means the game wasn't running —
  // an Intro owned the state, the app was backgrounded, a state handoff.
  // Those gaps used to land in the window as fps=0 lines with the whole
  // stall in "other" (Glenn's Android level-march logs: one bogus line
  // per skip-intro, plus a 5 s one for the intro auto-start). Restart
  // the window instead; genuine hitches are far below this bar.
  if (perf_last_frame_ && now - perf_last_frame_ > 500) {
    perf_window_start_ = now;
    perf_frames_ = 0;
    perf_tick_ms_ = perf_draw_ms_ = perf_tick_max_ = perf_draw_max_ = 0;
    perf_lens_ms_ = perf_lens_max_ = 0;
    perf_objs_pc_ = perf_stars_pc_ = perf_osd_pc_ = 0;
  }
  perf_last_frame_ = now;
  Uint32 span = now - perf_window_start_;
  if (span < 1000) return;
  // Below ~55 fps: say where the second went. tick+draw are CPU-side
  // (sim + GL submission); the remainder is swap/present (GPU-bound or
  // vsync). One line per second at most, only while slow.
  //
  // NEWTONIA_PERF_ALWAYS=1 drops the gate and reports every second
  // regardless. The gate is right for the field — a line only when
  // something is wrong — but it makes the breakdown unavailable exactly
  // when you want to compare two configurations that are BOTH fast
  // enough (1P vs 4P on a dev box, FOURPLAYER.md O6). Read once; no
  // shipped build sets it.
  static const bool perf_always = SDL_getenv("NEWTONIA_PERF_ALWAYS") != NULL;
  if (perf_always || perf_frames_ * 1000 < (int)span * 55) {
    Uint64 pcf = SDL_GetPerformanceFrequency();
    SDL_Log("perf: fps=%d tick=%ums(max %u) draw=%ums(max %u) "
            "objs=%ums stars=%ums osd=%ums lens=%ums(max %u) other=%dms "
            "asteroids=%u dead=%u pickups=%u gen=%d",
            (int)(perf_frames_ * 1000 / span), perf_tick_ms_, perf_tick_max_,
            perf_draw_ms_, perf_draw_max_,
            (unsigned)(perf_objs_pc_ * 1000 / pcf),
            (unsigned)(perf_stars_pc_ * 1000 / pcf),
            (unsigned)(perf_osd_pc_ * 1000 / pcf),
            perf_lens_ms_, perf_lens_max_,
            (int)(span - perf_tick_ms_ - perf_draw_ms_),
            (unsigned)objects->size(), (unsigned)dead_objects->size(),
            (unsigned)pickups->size(), generation);
  }
  perf_window_start_ = now;
  perf_frames_ = 0;
  perf_tick_ms_ = perf_draw_ms_ = 0;
  perf_tick_max_ = perf_draw_max_ = 0;
  perf_lens_ms_ = perf_lens_max_ = 0;
  perf_objs_pc_ = perf_stars_pc_ = perf_osd_pc_ = 0;
}

void GLGame::tick(int delta) {
  PerfScope perf_scope_(&perf_tick_ms_, &perf_tick_max_);
  // The camera's follow rate is a per-frame amount, and the frame it belongs
  // to is a SIMULATED one — draw() banks it from here rather than reading a
  // clock (see the note there).
  camera_delta_pending_ += delta;
  // Replay recording starts on the first tick — the earliest point that
  // knows the game's mode (the net ctors delegate to the offline ctors and
  // set net_mode_ only afterwards). Offline AND online games record
  // (replay_start branches on the mode); playback never does.
  if (!replay_tried_) {
    replay_tried_ = true;
    if (net_mode_ != NetReplay && !game_over) replay_start();
  }
  // Leaderboard game-over flow: poll the qualify/upload socket while the
  // GAME OVER card is up (board_ only exists after the game-over latch).
  if (board_) board_tick();
  if (net_mode_ != NetOff) {
    // Name a hole in OUR OWN tick cadence (App Nap on an occluded mac
    // window, a window drag, a debugger): in every other log line it is
    // indistinguishable from a network stall — it produces "input gap"
    // on the peer and correction bursts here.
    if (Net::net_debug_enabled() && delta > 250)
      NET_LOG("net: LOCAL frame stall %d ms\n", delta);
    else if (Net::net_debug_enabled() && delta > 50)
      NET_LOG("net: frame hitch %d ms\n", delta);  // visible stutter
    // Pin the simulation rate online: the =/- time cheats change
    // time_between_steps on ONE machine only, and a 8 vs 7 ms mismatch
    // makes every object drift continuously — permanent rubberbanding
    // that no reconciliation can hide.
    time_between_steps = step_size;
  }
  // Paused playback freezes the recorded world. Both things that advance it
  // — the replay clock in tick_replay_poll and the ghost extrapolation —
  // live inside tick_net_client, which never consulted `running`, so the
  // pause card used to sit over a world still playing on underneath (field:
  // Android, 2026-07-27). Not running it is the only thing that stops both.
  // current_time still advances so the card and the flashing exit row keep
  // animating, exactly as in a paused offline game (which banks current_time
  // before its own pause gate); keys and taps dispatch outside tick(), so the
  // transport controls and the exit stay live. NetClient is deliberately not
  // included — online, a local pause must not stop the peer's world.
  if (net_mode_ == NetReplay && !running) {
    current_time += delta;
    return;
  }
  if (net_mode_ == NetClient || net_mode_ == NetReplay) {
    if (net_mode_ == NetReplay) {
      // Playback speed scales time itself — clock, extrapolation and
      // banners all stretch together. Never a cheat (nothing is earned).
      delta = (int)(delta * replay_speed_);
      if (delta < 1) delta = 1;
    }
    tick_net_client(delta);
    return;
  }
  current_time += delta;

  // Drive a player fully out of lives so the revive/spectate flows and the
  // game-over paths can be exercised headlessly. Online, lives are
  // host-authoritative and replicate; "remote"/default = players->back()
  // (the joiner online, P2 offline), "local" = players->front(), and
  // "all" empties everyone — the deterministic game-over trigger the
  // leaderboard-prompt drivers need (a spray-scored run with a nearly
  // cleared field can survive blind crash loops indefinitely).
  // Two independent firings (…_KILL_MS / …_KILL2_MS), and WHO takes
  // "seatN" as well as local/remote/all, so a driver can STAGGER deaths:
  // the revive queue's order only means anything when players fall at
  // different times, and a single simultaneous wipe stamps them in seat
  // order — which is exactly the case that can't tell the longest-dead
  // pick apart from the old lowest-seat one.
  // The FILE trigger below is deliberately OUTSIDE the >= 2 players gate
  // (the revive hooks need a partner; emptying a player's lives does not),
  // so the solo leaderboard drivers can reach a real game over without
  // ramming asteroids and hoping — the same reason the time-slow hook
  // further down sits outside it. The timer keeps its co-op scope; see
  // below. Inert without its env vars, at any player count.
  {
    static int test_kill_ms[2] = {-2, -2};
    static int test_kill_who[2] = {1, 1};  // 0 local, 1 remote, 2 all, <0 seat
    // A bad WHO spec parks the slot here: no firing path — timer OR file —
    // may use it. Distinct from every live code (force_out would read an
    // unknown positive as "everyone", the exact quiet misfire the
    // validation exists to prevent).
    static const int kTestKillDisabled = 3;
    if (test_kill_ms[0] == -2) {
      const char *ms_var[2] = {"NEWTONIA_NET_TEST_KILL_MS",
                               "NEWTONIA_NET_TEST_KILL2_MS"};
      const char *who_var[2] = {"NEWTONIA_NET_TEST_KILL_WHO",
                                "NEWTONIA_NET_TEST_KILL2_WHO"};
      for (int k = 0; k < 2; k++) {
        const char *e = getenv(ms_var[k]);
        test_kill_ms[k] = e ? atoi(e) : -1;
        const char *who = getenv(who_var[k]);
        std::string w = who ? who : "";
        if (w.empty()) test_kill_who[k] = 1;
        else if (w == "local") test_kill_who[k] = 0;
        else if (w == "all") test_kill_who[k] = 2;
        else if (w.compare(0, 4, "seat") == 0) {
          // Validate: "seat", "seat0" and any typo atoi to 0, which is the
          // LOCAL code — the hook would quietly empty player 1 and log
          // "forcing local player", so a staggered-death driver could pass
          // or fail for the wrong reason. A bad spec disables the firing
          // loudly instead.
          int seat = atoi(w.c_str() + 4);
          if (seat >= 1 && seat <= MAX_PLAYERS) {
            test_kill_who[k] = -seat;
          } else {
            NET_LOG("net: TEST bad KILL WHO '%s' - firing disabled\n",
                    w.c_str());
            test_kill_ms[k] = -1;
            // ...and the WHO itself: the file trigger below fires
            // test_kill_who[0] without consulting the timer, so zeroing
            // only the ms left it firing the DEFAULT (remote) — not what
            // the driver named, and not disabled either.
            test_kill_who[k] = kTestKillDisabled;
          }
        } else {
          test_kill_who[k] = 1;
        }
      }
    }
    auto force_out = [&](int who) {
      if (who == kTestKillDisabled) return;  // parked by a bad WHO spec
      if (who < 0)
        NET_LOG("net: TEST forcing seat %d out of lives\n", -who);
      else
        NET_LOG("net: TEST forcing %s out of lives\n",
                who == 2 ? "everyone" : who ? "remote player" : "local player");
      for (auto *gs : *players) {
        if (who == 0 && gs != players->front()) continue;
        if (who == 1 && gs != players->back()) continue;
        if (who < 0 && (int)gs->ship->net_seat != -who) continue;
        gs->ship->lives = 0;
        gs->ship->kill();
      }
    };
    // The TIMER stays co-op-scoped: its countdown only advances with a
    // partner present, so the 20 s the existing drivers pass means 20 s
    // AFTER the join, not after launch. They all start as a lone host and
    // spend an unbounded stretch creating the room and handshaking, so
    // ticking this from process start would fire the kill early — before
    // the joiner has even arrived.
    if (players->size() >= 2) {
      for (int k = 0; k < 2; k++) {
        if (test_kill_ms[k] <= 0) continue;
        test_kill_ms[k] -= delta;
        if (test_kill_ms[k] > 0) continue;
        test_kill_ms[k] = -1;
        force_out(test_kill_who[k]);
      }
    }
    // A TIMER can only say "N ms from launch", which is no use when the
    // phase before the death has no fixed length: the leaderboard drivers
    // score a qualifying run first and re-spray until one lands, so
    // nothing knows in advance when the run should end. KILL_FILE lets the
    // driver say WHEN — it touches the file, and the players named by
    // …_KILL_WHO empty out on the next poll. Fires once; polled 4x/s and
    // only while the var is set, so a normal build never stats anything.
    static std::string test_kill_file;
    static int test_kill_file_state = -2;  // -2 unread, -1 off, 1 armed
    static int test_kill_file_poll = 0;
    if (test_kill_file_state == -2) {
      const char *f = getenv("NEWTONIA_NET_TEST_KILL_FILE");
      test_kill_file = f ? f : "";
      // A bad KILL_WHO disables this trigger too (the parse above already
      // logged it): it fires test_kill_who[0], and arming it anyway would
      // kill the default target instead of the seat the driver named.
      test_kill_file_state =
          (test_kill_file.empty() || test_kill_who[0] == kTestKillDisabled)
              ? -1 : 1;
    }
    if (test_kill_file_state == 1) {
      test_kill_file_poll -= delta;
      if (test_kill_file_poll <= 0) {
        test_kill_file_poll = 250;
        FILE *fp = fopen(test_kill_file.c_str(), "rb");
        if (fp) {
          fclose(fp);
          test_kill_file_state = -1;
          NET_LOG("net: TEST kill file seen\n");
          force_out(test_kill_who[0]);
        }
      }
    }
  }

  // Co-op e2e hooks, live OFFLINE and as host alike (they drive the shared
  // sim: the revive payload and a real revive pickup drop). All inert
  // without their env vars.
  if (players->size() >= 2) {
    // Apply the revive effect to whichever player is fully out — the
    // pickup-collection payload without the blind-navigation problem of
    // actually touching a pickup in a driver.
    static int test_revive_ms = -2;
    if (test_revive_ms == -2) {
      const char *e = getenv("NEWTONIA_NET_TEST_REVIVE_MS");
      test_revive_ms = e ? atoi(e) : -1;
    }
    if (test_revive_ms > 0) {
      test_revive_ms -= delta;
      if (test_revive_ms <= 0) {
        test_revive_ms = -1;
        NET_LOG("net: TEST applying revive\n");
        revive_fallen_partner(NULL);
      }
    }
    // ...and the FILE spelling of the same hook (the KILL_FILE pattern):
    // the phase before the revive has no fixed length — revive.sh sprays
    // until the drop actually LANDS — so a timer generous enough for the
    // slow case parked the host in a dense gen-4 field for ~50 s in the
    // fast one, where it was rammed out of its own lives and the timed
    // revive arrived to a finished game (CI, 2026-08-14; blind defensive
    // fire did not save it — gen 4 fields include invisible rocks). The
    // driver touches the file the moment the drop lands instead. Fires
    // once; polled 4x/s and only while the var is set.
    static std::string test_revive_file;
    static int test_revive_file_state = -2;  // -2 unread, -1 off, 1 armed
    static int test_revive_file_poll = 0;
    if (test_revive_file_state == -2) {
      const char *f = getenv("NEWTONIA_NET_TEST_REVIVE_FILE");
      test_revive_file = f ? f : "";
      test_revive_file_state = test_revive_file.empty() ? -1 : 1;
    }
    if (test_revive_file_state == 1) {
      test_revive_file_poll -= delta;
      if (test_revive_file_poll <= 0) {
        test_revive_file_poll = 250;
        FILE *fp = fopen(test_revive_file.c_str(), "rb");
        if (fp) {
          fclose(fp);
          test_revive_file_state = -1;
          NET_LOG("net: TEST applying revive\n");
          revive_fallen_partner(NULL);
        }
      }
    }
    // Spawn a real RevivePickup on a LIVING player so the ordinary
    // collision/collection path runs — the full field flow, minus the
    // random drop roll. NEWTONIA_NET_TEST_REVIVE_DROP_DIST offsets the
    // drop that many units away instead (0/absent = instant collection;
    // a distance lets a driver exercise the pickup SURVIVING something,
    // e.g. the generation rebuild).
    static int test_revive_drop_ms = -2, test_revive_drop_dist = 0;
    if (test_revive_drop_ms == -2) {
      const char *e = getenv("NEWTONIA_NET_TEST_REVIVE_DROP_MS");
      test_revive_drop_ms = e ? atoi(e) : -1;
      const char *d = getenv("NEWTONIA_NET_TEST_REVIVE_DROP_DIST");
      test_revive_drop_dist = d ? atoi(d) : 0;
    }
    if (test_revive_drop_ms > 0) {
      test_revive_drop_ms -= delta;
      if (test_revive_drop_ms <= 0) {
        test_revive_drop_ms = -1;
        for (auto *gs : *players) {
          if (!gs->ship->is_alive()) continue;
          NET_LOG("net: TEST dropping revive pickup near the living player\n");
          pickups->push_back(new RevivePickup(
              gs->ship->position + Point((float)test_revive_drop_dist, 0)));
          break;
        }
      }
    }
  }
  // Spawn a real TimeSlowPickup on a living player so the ordinary
  // collection path runs (collision -> start_time_slow -> snapshot
  // replication) — the revive-drop hook's pattern, but solo-friendly:
  // time slow works with any player count. Inert without the env var.
  {
    static int test_time_slow_ms = -2;
    if (test_time_slow_ms == -2) {
      const char *e = getenv("NEWTONIA_NET_TEST_TIME_SLOW_MS");
      test_time_slow_ms = e ? atoi(e) : -1;
    }
    if (test_time_slow_ms > 0) {
      test_time_slow_ms -= delta;
      if (test_time_slow_ms <= 0) {
        test_time_slow_ms = -1;
        for (auto *gs : *players) {
          if (!gs->ship->is_alive()) continue;
          NET_LOG("net: TEST dropping time-slow pickup on the living player\n");
          pickups->push_back(new TimeSlowPickup(gs->ship->position));
          break;
        }
      }
    }
  }

  // Online host: poll the peer before the pause gate — their RESUME (or a
  // dead transport) must be noticed even while paused.
  if (net_mode_ == NetHost) {
    // M3-1 e2e hooks: sever the transport / relay socket on a timer so the
    // recovery paths can be driven without killing either process. Inert
    // without the env vars.
    static int test_drop_transport = -2, test_drop_signal = -2;
    if (test_drop_transport == -2) {
      const char *e = getenv("NEWTONIA_NET_TEST_DROP_TRANSPORT_MS");
      test_drop_transport = e ? atoi(e) : -1;
    }
    if (test_drop_signal == -2) {
      const char *e = getenv("NEWTONIA_NET_TEST_DROP_SIGNAL_MS");
      test_drop_signal = e ? atoi(e) : -1;
    }
    if (test_drop_transport > 0 && net_session()) {
      test_drop_transport -= delta;
      if (test_drop_transport <= 0) {
        test_drop_transport = -1;
        NET_LOG("net: TEST dropping transport\n");
        net_session()->transport()->close();
      }
    }
    if (test_drop_signal > 0 && net_signal_) {
      test_drop_signal -= delta;
      if (test_drop_signal <= 0) {
        test_drop_signal = -1;
        NET_LOG("net: TEST dropping signal socket\n");
        net_signal_->close();          // local close emits no Closed event;
        net_signal_retry_ms_ = 700;    // a real drop arrives as Event::Closed
      }
    }

    // B5: the drain, pings and watchdogs are per PEER — one seat's death
    // must not gate another seat's traffic (net_host_poll and
    // net_ping_tick skip session-less/lost peers themselves).
    net_host_poll();
    // A kicked peer's goodbye needs a moment on the wire before its
    // transport goes (see net_kick_closing_). The seat is already parked
    // and free, so this only delays the teardown, nothing the game waits
    // on. Matched by SEAT, not by `lost && parked && session` — that
    // predicate is exactly net_handshaking_lost_peer(), so it would also
    // delete another seat's in-flight rejoin that happened to complete
    // inside this window. One entry per kicked seat (the lobby drain's
    // loop, NetLobby::pump_signal's closing_ sweep, is the twin).
    for (size_t ki = 0; ki < net_kick_closing_.size();) {
      net_kick_closing_[ki].second -= delta;
      if (net_kick_closing_[ki].second <= 0) {
        NetPeer *pk = net_peer_by_seat((int)net_kick_closing_[ki].first);
        if (pk && pk->lost && pk->parked && pk->session) net_drop_session(*pk);
        net_kick_closing_.erase(net_kick_closing_.begin() + ki);
      } else {
        ki++;
      }
    }
    // The door's refusals draining (see net_closing_).
    for (size_t ci = 0; ci < net_closing_.size();) {
      net_closing_[ci].second -= delta;
      if (net_closing_[ci].second <= 0) {
        delete net_closing_[ci].first;  // closes + deletes the transport
        net_closing_.erase(net_closing_.begin() + ci);
      } else {
        ci++;
      }
    }
    net_ping_tick(delta);
    for (NetPeer *pw : net_peers_) {
      if (pw->lost || !pw->session) continue;
      // RX watchdog, host side: a client whose game runs sends INPUT at
      // 125 Hz (+ the reliable mirror); 10 s of silence while WE are
      // running is a dead path, not a quiet player. Paused games pause
      // both sides, and the pause gate below stops this tick anyway.
      if (running && pw->have_input &&
          current_time - pw->last_input_time > 10000) {
        NET_LOG("net: RX watchdog - 10 s without INPUT, seat %d lost\n",
                (int)pw->seat);
        // Close actively: the path may be one-way dead (or the peer
        // frozen) with OUR transport still nominally alive — the close
        // is what makes the peer's side fail fast and auto-rejoin
        // instead of playing into the void.
        pw->session->transport()->close();
        pw->lost = true;
      }
      if (pw->session->transport()->failed()) pw->lost = true;
    }
    if (net_signal_ && !net_any_peer_lost()) net_host_signal_maintain(delta);
    if (net_any_peer_lost()) {
      // No door can open (worker-less session, LAN hidden/unavailable)
      // and nobody is left: terminal, exactly as before B5 — no park, no
      // pause, no misleading "awaiting rejoin" log; the draw shows the
      // CONNECTION LOST card over the frozen world.
      bool lan_possible = net_mode_ == NetHost && NetLan::available() &&
                          g_prefs.lan_visible;
      if (!net_signal_ && !lan_possible && net_all_peers_lost())
        return;
      // Park every newly lost seat before the doors look at the roster
      // (park drops the dead session, which is what the door-arm keys
      // on). Pause only happens inside when the LAST live peer went.
      for (NetPeer *pw : net_peers_)
        if (pw->lost && !pw->parked) net_host_rejoin_park_peer(*pw);
      // Both rejoin doors, mirroring the lobby: the relay room (when the
      // session came through one) and the LAN beacon (wherever LAN
      // discovery exists — including sessions that STARTED on the LAN
      // door, which have no signal at all; that loss used to be
      // terminal). Whichever door's re-pair completes is adopted by the
      // shared session update.
      bool lan_door = net_host_lan_rejoin_poll(delta);
      if (net_signal_)
        net_host_rejoin_poll(delta);  // room open: play on, await rejoin
      else if (!lan_door && net_all_peers_lost())
        return;  // no door to reopen; draw shows CONNECTION LOST
      // B5: with another peer still live the doorless loss is NOT
      // terminal — play on without that seat (its hull stays parked).
      net_host_rejoin_session_update(delta);
    }
    // Steam invite: while the peer is gone but the room is still open for
    // rejoin (net_signal_ live), re-advertise as joinable so a friend — or
    // the dropped peer — can drop into the empty co-op slot via a platform
    // Join. Edge-detected so the "connect" key is written only on the
    // transitions, not every frame; cleared again the moment the slot fills
    // (net_any_peer_lost() back to false) or the room is truly lost.
    bool joinable_now =
        net_any_peer_lost() && net_signal_ != NULL && !net_room_code_.empty();
    if (joinable_now && !net_invite_advertised_) {
      Invites::set_joinable(net_room_code_);
      net_invite_advertised_ = true;
      NET_LOG("net: invite - room %s joinable (peer gone)\n",
              net_room_code_.c_str());
    } else if (!joinable_now && net_invite_advertised_) {
      Invites::clear_joinable();
      net_invite_advertised_ = false;
      NET_LOG("net: invite - room no longer joinable\n");
    }
    // Process-death resume: keep the ticket's timestamp fresh (a ~100
    // byte write) while the room is reclaimable, so a relaunch can tell
    // "host died moments ago, grace still open" from a stale leftover.
    // The world save stays on the auto-save moments (save_progress).
    if (net_signal_ && !net_room_token_.empty() && !game_over) {
      net_resume_ticket_ms_ -= delta;
      if (net_resume_ticket_ms_ <= 0) {
        net_resume_ticket_ms_ = 10000;
        NetResume::write(net_room_code_, net_room_token_);
      }
    }
    if (net_banner_ms_ > 0) net_banner_ms_ -= delta;
  }

  if (!running) {
    last_tick += delta;
    // A paused client legitimately goes quiet (tick_net_client returns
    // before its INPUT send while !running), but current_time keeps
    // advancing here — without this refresh the RX watchdog's baseline
    // ages by the whole pause and the first running tick after any >10 s
    // pause kills a healthy session (seen as a spurious loss/rejoin cycle
    // the moment a resumed host unpaused). Mirrors the client's paused
    // net_last_rx_time_ refresh.
    // B5: EVERY peer's baseline — the watchdog runs per seat now, so a
    // front-only refresh would kill seats 3+ on the first unpause tick.
    if (net_mode_ == NetHost)
      for (NetPeer *pw : net_peers_) pw->last_input_time = current_time;
    return;
  }

  save_dirty_ = true;  // the sim advances below; the save on disk is stale

  time_until_next_step -= delta;

  num_frames++;

  // The level is only clear once every killable asteroid AND every hazard
  // (pulsar/comet/seeker) is destroyed — the hazards must be hunted down too.
  bool hazards_pending = false;
  for(auto* h : *hazards)
    if(h->is_alive()) { hazards_pending = true; break; }

  if(Asteroid::num_killable == 0 && !hazards_pending) {
    if(!level_cleared) {
      // A client kill-claim (PROTO 13) is applied in net_host_poll —
      // BEFORE this check — but the killed asteroid's breakup runs in the
      // step-loop reap AFTER it, so num_killable dips to 0 for one tick
      // with fragments still incoming. Latching CLEARED here freezes the
      // countdown (the decrement lives only inside this num_killable==0
      // block) until those un-counted fragments happen to die — which,
      // with the claimer dead, they never do. Defer the latch while any
      // just-killed asteroid still owes fragments (Glenn: host stuck on
      // "CLEARED" at gen 0 after the client rammed the last rocks).
      bool owed_fragments = false;
      for (Asteroid *a : *objects)
        if (a->pending_fragments()) { owed_fragments = true; break; }
      if (!owed_fragments) {
        level_cleared = true;
        time_until_next_generation = 5000;
        // Generation cleared legitimately — the skip-level cheat sets
        // level_cleared directly and never reaches this branch. unlock() itself
        // still suppresses if any cheat was used this game (XR-057).
        // User-facing criteria use displayed level numbers = generation + 1
        // (ACHIEVEMENTS.md §5 terminology rule).
        if(generation == 0) Achievements::unlock("clear_level1");
        if(players->size() >= 2) Achievements::unlock("coop_clear");
        bool local_died = false, local_survived = false;
        for(auto *gs : *players) {
          if(!gs->ship->is_local_player) continue;
          if(gs->ship->died_this_generation) local_died = true;
          else local_survived = true;
        }
        // "Clear level 9 or beyond without taking damage" — level 9 = generation 8.
        if(generation >= 8 && !local_died) Achievements::unlock("no_damage_clear");
        // Black hole exists from level 14 (generation 13); any local player
        // surviving the whole level counts (2P criteria per the §5 re-pitch note).
        if(generation >= 13 && local_survived) Achievements::unlock("black_hole_survivor");
      } else if (Net::net_debug_enabled()) {
        NET_LOG("net: level-clear deferred - killed rock still owes "
                "fragments (would have stuck CLEARED)\n");
      }
    } else if (time_until_next_generation > 0) {
      if(floor(time_until_next_generation/1000) != floor((time_until_next_generation-delta)/1000)) {
        if(tic_sound != NULL) {
          Mix_PlayChannel(-1, tic_sound, 0);
        }
        net_send_event(Net::EV_LEVEL_TIC);  // the client mirrors the countdown
      }
      time_until_next_generation -= delta;
    } else {
      generation++;
      update_presence();
      // The enemy station arrives at generation 14 (displayed level 15) and
      // the world takes its one big growth jump to make room for it.
      if(generation == 14) {
        world += Point(3000, 3000);
      } else {
        world += Point(50, 50);
      }
      grid = Grid(world, Point(Asteroid::max_radius*2,Asteroid::max_radius*2));
      if(generation >= 14) {
        if(station != NULL)
          delete station;
        station = new GLStation(grid, enemies, players, (std::list<Object*>*)objects,
                                hostile_aim_lead(generation));
      }
      if(station != NULL) {
        station->reset();
      }
      delete starfield;
      starfield = new GLStarfield(world, star_density_scale());
      WrappedPoint::set_boundaries(world);
      while(!objects->empty()) {
        delete objects->back();
        objects->pop_back();
      }
      while(!dead_objects->empty()) {
        delete dead_objects->back();
        dead_objects->pop_back();
      }
      Asteroid::num_killable = 0;
      add_asteroids();
      grid.update((std::list<Object *>*)objects);
      // From generation 10, spawn a small roaming station with a fresh random
      // heading each generation. Created here, after the new world bounds and
      // asteroids are in place, so it gets a valid random starting position
      // inside the new world.
      if(generation >= 10) {
        if(mini_station != NULL)
          delete mini_station;
        mini_station = new GLMiniStation(grid, players, (std::list<Object*>*)objects,
                                         hostile_aim_lead(generation));
      }
      // Rebuild the mid-game hazards for the new generation (fresh positions,
      // like the mini-station gets a fresh heading each level).
      while(!hazards->empty()) {
        delete hazards->back();
        hazards->pop_back();
      }
      add_hazards();
      while(!pickups->empty()) {
        delete pickups->back();
        pickups->pop_back();
      }
      // Reposition the black hole at the new world centre.
      while(!black_holes->empty()) {
        delete black_holes->back();
        black_holes->pop_back();
      }
      if(generation >= 13)
        black_holes->push_back(new BlackHole(WrappedPoint(world.x() / 2.0f, world.y() / 2.0f)));
      std::list<GLShip*>::iterator o;
      for(o = players->begin(); o != players->end(); o++) {
        // Deployed turrets end with the level, like the pickups above:
        // they survive their owner's respawn (reset() spares them), so
        // the rollover is the one place they are swept — the new world
        // has new bounds, and a survivor could sit outside them. Silent
        // by design; the net client's wipe arrives as this rebuild's
        // QUIET apply, so no debris fires on either machine.
        (*o)->ship->turrets.clear();
        (*o)->ship->respawn(grid, false);
        (*o)->ship->died_this_generation = false;
      }
      // Suppressed (like every unlock) for the rest of the game once any
      // cheat key has been used — deliberately NOT reset per generation, or
      // skipping to one generation short and clearing a single level would
      // unlock the progression achievements (XR-057). "Reach level 15":
      // displayed level = generation + 1.
      Achievements::progress("reach_level15", (generation + 1) * 100 / 15);
      // "Reach level 10 without using a secondary weapon" — secondary kinds
      // are Mine..Nova + Turret in weapons_fired_mask — per-game, per-player,
      // and saved, so a resumed game keeps its usage history. Level-triggered
      // (>= 9) so a still-clean mask keeps qualifying on later rebuilds.
      if(generation >= 9) {
        const uint32_t secondary_bits =
            (1u << (int)Save::WeaponEntry::Kind::Mine) |
            (1u << (int)Save::WeaponEntry::Kind::GigaMine) |
            (1u << (int)Save::WeaponEntry::Kind::Missile) |
            (1u << (int)Save::WeaponEntry::Kind::Shield) |
            (1u << (int)Save::WeaponEntry::Kind::Nova) |
            (1u << (int)Save::WeaponEntry::Kind::Turret);
        for(o = players->begin(); o != players->end(); o++) {
          Ship *s = (*o)->ship;
          if(s->is_local_player && (s->weapons_fired_mask & secondary_bits) == 0) {
            Achievements::unlock("no_secondary_level10");
            break;
          }
        }
      }
      level_cleared = false;
      save_progress();
      // Ungated: online the host still sends it, and offline the replay tee
      // records the generation marker (the call is a no-op with no session
      // and no recorder).
      net_send_event(Net::EV_GENERATION_START, (uint32_t)generation);
      // The world was rebuilt: deltas would reference dead ids — both the
      // net client and the replay reader need a fresh keyframe.
      net_force_keyframe_ = true;
      // Replay checkpoint (REPLAY.md): the level boundary drains the record
      // chunk to disk. When an intro follows (levels 1-15) this lands
      // immediately before it — the frozen intro is the slack window.
      if (replay_) replay_->flush();
      maybe_start_intro();
      if (net_mode_ == NetHost) {
        net_set_generation_banner(generation);
        // Same restriction as the local player: respawn's reset() cleared
        // the remote ships' controls; don't let the still-held INPUT bits
        // re-arm them 8 ms later — each key must be released and
        // re-pressed. B5: every seat, not just the front peer.
        for (NetPeer *pw : net_peers_) pw->held_suppress = 0xffff;
      }
      if (is_finished()) {
        // Handing off to an intro: freeze here and give back the delta so no
        // catch-up steps (or their sounds, just muted by the intro) run.
        time_until_next_step += delta;
        last_tick += delta;
        return;
      }
    }
  }

  std::list<GLShip*>::iterator o, o2;
  while(time_until_next_step <= 0) {
	/* STEP EVERYTHING */

    if(station != NULL) {
      station->step(step_size, grid);
    }

    if(mini_station != NULL) {
      // Attenuate its shot sound: nearest player (solo/split), or the
      // local player only when hosting online.
      mini_station->sound_volume_scale = world_volume(mini_station->position);
      mini_station->sound_own_cues = false;  // not a ship anyone is flying
      mini_station->step(step_size, grid);
    }

    // Step black holes (visual animation only).
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      (*bhi)->step(step_size);
    }

    // Advance the mid-game hazards (pulsar cycle, comet drift, seeker homing).
    for(auto* h : *hazards) {
      h->update(step_size, players);
    }

    std::list<Asteroid*>::iterator oi;
    for(oi = objects->begin(); oi != objects->end(); oi++) {
      (*oi)->step(step_size);
    }
    for(oi = dead_objects->begin(); oi != dead_objects->end(); oi++) {
      (*oi)->step(step_size);
    }

    // Apply black-hole gravity to asteroids (asteroids pass through, not swallowed).
    // Invincible asteroids are unaffected. Asteroids inside a well keep full
    // slingshot dynamics; the excess-speed decay in Asteroid::step() only runs
    // once they are free, so re-flag in_gravity_well every tick.
    for(oi = objects->begin(); oi != objects->end(); oi++)
      (*oi)->in_gravity_well = false;
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      oi = objects->begin();
      while(oi != objects->end()) {
        if(!(*oi)->invincible) {
          (*bhi)->apply_gravity(**oi, step_size);
          if((*oi)->position.distance_to((*bhi)->position) < BlackHole::influence_radius)
            (*oi)->in_gravity_well = true;
        }
        oi++;
      }
    }

    // NEWTONIA_ALL_WEAPONS: give each player the full arsenal at 999 rounds.
    // A death strips the ship back to the base gun (reset()), so re-grant the
    // moment a freshly-respawned player is detected — that also seeds it once
    // at game start. GLGame is a friend of Ship, so it can inspect the lists.
    if(all_weapons_cheat) {
      for(o = players->begin(); o != players->end(); o++) {
        Ship* s = (*o)->ship;
        if(s->is_alive() && s->primary_weapons.size() <= 1 && s->secondary_weapons.empty())
          s->give_all_weapons(all_weapons_ammo);
      }
    }

    // Refresh the shock-bolt seek list: every hostile a player's lightning may
    // path to this frame (enemy ships, the station, the mini-station, the
    // mid-game hazards). Asteroids are seeked separately via each ship's
    // missile-asteroid list. With friendly fire on, other players join the list
    // too — each bolt skips its own owner, so a ship's lightning only arcs to the
    // *other* player, matching bullets.
    shock_targets->clear();
    for(o = enemies->begin(); o != enemies->end(); o++)
      shock_targets->push_back((*o)->ship);
    if(station != NULL && station->is_alive())
      shock_targets->push_back(station);
    if(mini_station != NULL && mini_station->is_alive())
      shock_targets->push_back(mini_station);
    for(auto* h : *hazards)
      if(h->is_alive())
        shock_targets->push_back(h);
    if(friendly_fire)
      for(o = players->begin(); o != players->end(); o++)
        shock_targets->push_back((*o)->ship);

    update_player_sound_volumes();
    for(o = players->begin(); o != players->end(); o++) {
      (*o)->step(step_size, grid);
    }
    {
      // One audible respawn-countdown tic per step, however many ships are
      // counting down in sync (see Ship::respawn_tic_pending).
      bool tic_played = false, low_played = false;
      for(o = players->begin(); o != players->end(); o++)
        (*o)->ship->flush_respawn_tic(tic_played, low_played);
    }
    // v12 adopt smoothing: a post-blackout pose catch-up banked by
    // net_host_poll glides in instead of hopping on this screen. Every
    // remote hull, by seat (B7): the back()-only form drained one peer,
    // and a catch-up banked on a non-back seat sat undrained forever —
    // each INPUT recomputed err against the stale pose, landed back in
    // the 40-600 band, and REPLACED the debt, so the hull rode hundreds
    // of units from its pilot in the host sim and every snapshot.
    if (net_mode_ == NetHost)
      for (NetPeer *pr : net_peers_) {
        GLShip *gs = player_by_seat((int)pr->seat);
        if (gs) net_smooth_step(*gs->ship, step_size);
      }

    // Apply black-hole gravity to ships.
    // Ships in god mode or using the shield receive reduced gravity so they can escape.
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      for(o = players->begin(); o != players->end(); o++) {
        if(!(*o)->ship->is_alive()) continue;
        float scale = ((*o)->ship->god_mode_time_remaining() > 0 || (*o)->ship->shield_active()) ? 0.25f : 1.0f;
        if((*bhi)->apply_gravity(*(*o)->ship, step_size, scale)) {
          (*o)->ship->kill_stop();
        }
      }
      for(o = enemies->begin(); o != enemies->end(); o++) {
        if(!(*o)->ship->is_alive()) continue;
        float scale = ((*o)->ship->god_mode_time_remaining() > 0 || (*o)->ship->shield_active()) ? 0.25f : 1.0f;
        if((*bhi)->apply_gravity(*(*o)->ship, step_size, scale)) {
          (*o)->ship->kill();
        }
      }
    }

    // Apply black-hole gravity to bullets, missiles, mines, and turrets.
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      for(o = players->begin(); o != players->end(); o++) {
        for(auto &b : (*o)->ship->bullets)
          (*bhi)->apply_gravity(b, step_size);
        for(auto &m : (*o)->ship->missiles)
          (*bhi)->apply_gravity(m, step_size);
        for(auto &n : (*o)->ship->mines)
          (*bhi)->apply_gravity(n, step_size);
        for(auto &n : (*o)->ship->giga_mines)
          (*bhi)->apply_gravity(n, step_size);
        // Turrets fall like every other deployed object (they used to be
        // the one deployable the hole could not move — a drone parked in
        // the gravity well sat immune while its own bullets curved away).
        // The horizon-crossing return is ignored, the mine treatment.
        for(auto &t : (*o)->ship->turrets)
          (*bhi)->apply_gravity(t, step_size);
      }
      for(o = enemies->begin(); o != enemies->end(); o++) {
        for(auto &b : (*o)->ship->bullets)
          (*bhi)->apply_gravity(b, step_size);
        for(auto &m : (*o)->ship->missiles)
          (*bhi)->apply_gravity(m, step_size);
        for(auto &n : (*o)->ship->mines)
          (*bhi)->apply_gravity(n, step_size);
        for(auto &n : (*o)->ship->giga_mines)
          (*bhi)->apply_gravity(n, step_size);
      }
    }

    for(o = enemies->begin(); o != enemies->end(); o++) {
      Ship* s = (*o)->ship;
      // Online host: attenuate by distance to the LOCAL player — the
      // visibility test counts the remote player's view too, which
      // plays full-volume shots from someone else's dogfight.
      s->sound_volume_scale = all_players_local()
          ? (is_visible_to_any_player(*s) ? 0.5f : 0.0f)
          : 0.5f * net_listener_volume(s->position);
      s->sound_own_cues = false;  // not a ship anyone is flying
      (*o)->step(step_size, grid);
      // Re-level the thruster hum after the step: thrust() inside it fires
      // only on a control change, and the distance in the scale has moved.
      s->update_boost_volume();
    }

    /* UPDATE COLLISION MAP */

    grid.update((std::list<Object *>*)objects);

  /* ELASTIC ASTEROID-ASTEROID COLLISIONS */
    elastic_asteroid_collisions(/*announce=*/true);

  /* COLLIDE EVERYTHING */
    for(o = players->begin(); o != players->end(); o++) {
      (*o)->collide_grid(grid, step_size);
    }
    for(o = enemies->begin(); o != enemies->end(); o++) {
      (*o)->collide_grid(grid, step_size);
    }

    oi = objects->begin();
    while(oi != objects->end()) {
      if((*oi)->add_children(objects)) {
        if(!(*oi)->invincible) {
          // Co-op revive (its own roll, ahead of the normal table): only
          // while some player is fully out with a partner still in it, and
          // never more than one in the world at a time.
          bool revive_due = false;
          if (players->size() >= 2) {
            bool any_out = false, any_in = false;
            for (auto *gs : *players) {
              if (!gs->ship->is_alive() && gs->ship->lives <= 0) any_out = true;
              else if (gs->ship->is_alive() || gs->ship->lives > 0) any_in = true;
            }
            if (any_out && any_in) {
              revive_due = true;
              for (auto *pk : *pickups)
                if (dynamic_cast<RevivePickup*>(pk)) { revive_due = false; break; }
            }
          }
          if (revive_due &&
              rand() / float(RAND_MAX) < revive_pickup_drop_chance) {
            pickups->push_back(new RevivePickup((*oi)->position));
            NET_LOG("revive pickup dropped\n");
          } else {
          float roll = rand() / float(RAND_MAX);
          if(roll < extra_life_drop_chance) {
            pickups->push_back(new ExtraLife((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance) {
            int weapon_index = Ship::random_drop_weapon_index();
            pickups->push_back(new WeaponPickup((*oi)->position, weapon_index));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance) {
            pickups->push_back(new MinePickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance) {
            pickups->push_back(new GigaMinePickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance) {
            pickups->push_back(new MissilePickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance) {
            pickups->push_back(new ShieldPickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance + god_mode_pickup_drop_chance) {
            pickups->push_back(new GodModePickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance + god_mode_pickup_drop_chance + beam_pickup_drop_chance) {
            pickups->push_back(new BeamPickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance + god_mode_pickup_drop_chance + beam_pickup_drop_chance + lance_pickup_drop_chance) {
            pickups->push_back(new LancePickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance + god_mode_pickup_drop_chance + beam_pickup_drop_chance + lance_pickup_drop_chance + shock_pickup_drop_chance) {
            pickups->push_back(new ShockPickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance + god_mode_pickup_drop_chance + beam_pickup_drop_chance + lance_pickup_drop_chance + shock_pickup_drop_chance + time_slow_pickup_drop_chance) {
            pickups->push_back(new TimeSlowPickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance + god_mode_pickup_drop_chance + beam_pickup_drop_chance + lance_pickup_drop_chance + shock_pickup_drop_chance + time_slow_pickup_drop_chance + turret_pickup_drop_chance) {
            pickups->push_back(new TurretPickup((*oi)->position));
          }
          }
        }
        // Move to dead_objects so the collision grid no longer iterates this
        // asteroid while its debris particles are still fading out.
        if(!(*oi)->is_removable()) {
          dead_objects->push_back(*oi);
          oi = objects->erase(oi);
          continue;
        }
      }
      if((*oi)->is_removable()) {
        delete *oi;
        oi = objects->erase(oi);
      } else {
        oi++;
      }
    }

    // Spawn nova charge pickups earned this frame (every 100 asteroid kills per ship).
    for(o = players->begin(); o != players->end(); o++) {
      Ship *s = (*o)->ship;
      for(const Point &pos : s->nova_drops_pending) {
        pickups->push_back(new NovaChargePickup(WrappedPoint(pos.x(), pos.y())));
      }
      s->nova_drops_pending.clear();
    }

    // Handle teleporting asteroids: relocate in the direction of the arrow.
    for(oi = objects->begin(); oi != objects->end(); ++oi) {
      Asteroid *ast = *oi;
      if(!ast->teleport_pending) continue;
      // Travel in teleport_angle direction, random distance from 200 to half world size.
      const float min_dist_from_ship = 400.0f;
      const float max_travel = fminf(world.x(), world.y()) * 0.5f;
      const float min_travel = 200.0f;
      WrappedPoint new_pos;
      for(int tries = 0; tries < 30; tries++) {
        float dist = min_travel + (rand() / (float)RAND_MAX) * (max_travel - min_travel);
        float ox = cosf(ast->teleport_angle) * dist;
        float oy = sinf(ast->teleport_angle) * dist;
        new_pos = WrappedPoint(ast->position.x() + ox, ast->position.y() + oy);
        new_pos.wrap();
        bool safe = true;
        for(auto po = players->begin(); po != players->end(); ++po) {
          if((*po)->ship->is_alive() &&
             new_pos.distance_to((*po)->ship->position) < min_dist_from_ship) {
            safe = false;
            break;
          }
        }
        if(safe) break;
      }
      ast->position = new_pos;
      ast->teleport_vulnerable = true;
      ast->vulnerable_time_left = 5000; // 5 seconds of vulnerability
      ast->teleport_pending = false;
      ast->teleport_angle = rand() / (float)RAND_MAX * 2.0f * (float)M_PI;
      if(warp_sound != NULL)
        Mix_PlayChannel(-1, warp_sound, 0);
    }

    // Update quantum asteroid observation state (shared with the net
    // client's mirror — see update_quantum_observation).
    update_quantum_observation();

    // Clean up dead asteroids whose debris has fully faded.
    oi = dead_objects->begin();
    while(oi != dead_objects->end()) {
      if((*oi)->is_removable()) {
        delete *oi;
        oi = dead_objects->erase(oi);
      } else {
        oi++;
      }
    }

    // Lance ship/station hits: authoritative firers parked their traced
    // polyline during the step (the ray-march only sees asteroids); a
    // client's polyline reaches resolve_lance_ship_hits via MSG_LANCE.
    for(o = players->begin(); o != players->end(); o++) {
      Ship *ls = (*o)->ship;
      if(!ls->lance_hit_pending.empty()) {
        resolve_lance_ship_hits(ls, ls->lance_hit_pending);
        ls->lance_hit_pending.clear();
      }
    }

    for(o = players->begin(); o != players->end(); o++) {
      if(friendly_fire) {
        for(o2 = o; o2 != players->end(); o2++) {
          if(*o != *o2) {
            GLShip::collide(*o, *o2);
          }
        }
      }
      for(o2 = enemies->begin(); o2 != enemies->end(); o2++) {
        GLShip::collide(*o, *o2);
      }
    }

    /* COLLIDE PLAYERS AND BULLETS WITH STATION */
    if (station != NULL && station->is_alive()) {
      for (o = players->begin(); o != players->end(); o++) {
        Ship* s = (*o)->ship;
        // Body collision: mirrors invincible-asteroid behaviour — impact
        // particles and velocity stop always; death particles only if killed.
        if (s->is_alive() && station->Object::collide(*s)) {
          station->hit();
          s->explode(s->position, station->velocity);
          s->kill_stop();
          if (!s->is_alive())
            s->detonate();
        }
        // Missile collision: consume missile, deal 1 damage, scatter debris
        // (debris bullets are then picked up by the bullet loop below)
        if (!station->is_alive()) break;
        for (size_t i = 0; i < s->missiles.size(); ) {
          if (station->Object::collide(s->missiles[i])) {
            station->hit();
            s->detonate(s->missiles[i].position, s->missiles[i].velocity, Ship::MISSILE_SHRAPNEL);
            if (s->missile_explode_sound != NULL)
              Mix_PlayChannel(-1, s->missile_explode_sound, 0);
            s->missiles[i] = std::move(s->missiles.back());
            s->missiles.pop_back();
            if (!station->is_alive()) break;
          } else {
            ++i;
          }
        }
        // Bullet collision: consume bullet, damage station
        if (!station->is_alive()) break;
        for (size_t i = 0; i < s->bullets.size(); ) {
          if (station->Object::collide(s->bullets[i])) {
            // Reflect debris off the station surface normal
            Point bpos = s->bullets[i].position;
            Point bvel = s->bullets[i].velocity;
            Point normal = Point(bpos.x() - station->position.x(),
                                 bpos.y() - station->position.y()).normalized();
            float dot = bvel.x() * normal.x() + bvel.y() * normal.y();
            Point reflected(bvel.x() - 2.0f * dot * normal.x(),
                            bvel.y() - 2.0f * dot * normal.y());
            s->explode(bpos, reflected.normalized() * station->velocity.magnitude());
            if (Asteroid::thud_sound != NULL) {
              // Rate-limit like every other thud/ting site: sustained fire on
              // the station otherwise grabs a mixer channel per bullet and
              // starves the pool (audio "drops out" mid-siege). The net
              // relay rides the same limiter — the client plays one cue per
              // window too instead of one per bullet.
              static Uint32 last_station_thud = UINT32_MAX;
              Uint32 now = SDL_GetTicks();
              if (now - last_station_thud >= 125) {
                last_station_thud = now;
                WorldSound::play(Asteroid::thud_sound, bpos);
                // Station-hull deflection: same thud cue for the net client.
                net_send_event(Net::EV_ROID_THUD, Net::pack_pos(bpos.x(), bpos.y(), world.x(), world.y()));
              }
            }
            station->hit();
            s->bullets[i] = std::move(s->bullets.back());
            s->bullets.pop_back();
            if (!station->is_alive()) {
              if (s->is_local_player) Achievements::unlock("station_destroyed");
              else if (NetPeer *pr = net_peer_for_ship(s))
                net_send_event_to(*pr, Net::EV_ACHIEVEMENT,
                                  Net::ACH_STATION_DESTROYED);
              break;
            }
          } else {
            ++i;
          }
        }
      }
    }

    /* COLLIDE SHIPS AND PLAYER SHOTS WITH MID-GAME HAZARDS */
    for(auto* h : *hazards) {
      switch(h->kind_of()) {
        case Hazard::PULSAR: {
          if(!h->is_alive()) break;
          // The expanding shockwave *shoves* every ship its front reaches
          // outward — it never kills directly, but the push can fling you into
          // asteroids, a black hole, or another hazard.
          if(h->wave_active()) {
            float wr = h->wave_radius();
            for(o = players->begin(); o != players->end(); o++) {
              Ship* s = (*o)->ship;
              if(!s->is_alive()) continue;
              if(fabsf(s->position.distance_to(h->position) - wr) > Hazard::WAVE_BAND) continue;
              Point self = h->position.closest_to(s->position);
              Point out(s->position.x() - self.x(), s->position.y() - self.y());
              float m = out.magnitude();
              if(m > 1e-4f) s->velocity += (out / m) * (Hazard::KNOCKBACK * step_size);
            }
            for(o = enemies->begin(); o != enemies->end(); o++) {
              Ship* s = (*o)->ship;
              if(!s->is_alive()) continue;
              if(fabsf(s->position.distance_to(h->position) - wr) > Hazard::WAVE_BAND) continue;
              Point self = h->position.closest_to(s->position);
              Point out(s->position.x() - self.x(), s->position.y() - self.y());
              float m = out.magnitude();
              if(m > 1e-4f) s->velocity += (out / m) * (Hazard::KNOCKBACK * step_size);
            }
          }
          // Player shots break the stationary core (health-gated like the comet).
          for(o = players->begin(); o != players->end() && h->is_alive(); o++) {
            Ship* s = (*o)->ship;
            for(size_t i = 0; i < s->bullets.size();) {
              if(h->is_alive() && h->Object::collide(s->bullets[i])) {
                s->explode(s->bullets[i].position, Point());
                s->bullets[i] = std::move(s->bullets.back());
                s->bullets.pop_back();
                h->hit();
                if(h->is_alive()) play_hazard_hit_sound(Asteroid::thud_sound);
                else              s->score += Hazard::PULSAR_REWARD;
              } else { ++i; }
            }
            for(size_t i = 0; i < s->missiles.size() && h->is_alive();) {
              if(h->Object::collide(s->missiles[i])) {
                s->detonate(s->missiles[i].position, s->missiles[i].velocity, Ship::MISSILE_SHRAPNEL);
                if(s->missile_explode_sound != NULL)
                  Mix_PlayChannel(-1, s->missile_explode_sound, 0);
                s->missiles[i] = std::move(s->missiles.back());
                s->missiles.pop_back();
                h->hit();
                if(h->is_alive()) play_hazard_hit_sound(Asteroid::thud_sound);
                else              s->score += Hazard::PULSAR_REWARD;
              } else { ++i; }
            }
          }
          if(!h->is_alive() && station_explode_sound != NULL)
            Mix_PlayChannel(-1, station_explode_sound, 0);
          break;
        }
        case Hazard::COMET: {
          // Ploughs through ships (lethal unless invincible) but breaks up after
          // COMET_HEALTH shots, paying a bounty.
          if(!h->is_alive()) break;
          for(o = players->begin(); o != players->end(); o++) {
            Ship* s = (*o)->ship;
            if(s->is_alive() && h->Object::collide(*s)) {
              s->explode(s->position, h->velocity);
              s->kill_stop();
              if(!s->is_alive()) s->detonate();
            }
          }
          for(o = enemies->begin(); o != enemies->end(); o++) {
            Ship* s = (*o)->ship;
            if(s->is_alive() && h->Object::collide(*s)) s->kill();
          }
          // Player shots chip away at it.
          for(o = players->begin(); o != players->end() && h->is_alive(); o++) {
            Ship* s = (*o)->ship;
            for(size_t i = 0; i < s->bullets.size();) {
              if(h->is_alive() && h->Object::collide(s->bullets[i])) {
                s->explode(s->bullets[i].position, h->velocity);
                s->bullets[i] = std::move(s->bullets.back());
                s->bullets.pop_back();
                h->hit();
                shed_comet_fragment(h);   // knock a couple of chunks loose
                shed_comet_fragment(h);
                if(h->is_alive()) play_hazard_hit_sound(Asteroid::explode_sound);
                else              s->score += Hazard::COMET_REWARD;
              } else { ++i; }
            }
            for(size_t i = 0; i < s->missiles.size() && h->is_alive();) {
              if(h->Object::collide(s->missiles[i])) {
                s->detonate(s->missiles[i].position, s->missiles[i].velocity, Ship::MISSILE_SHRAPNEL);
                if(s->missile_explode_sound != NULL)
                  Mix_PlayChannel(-1, s->missile_explode_sound, 0);
                s->missiles[i] = std::move(s->missiles.back());
                s->missiles.pop_back();
                h->hit();
                shed_comet_fragment(h);
                shed_comet_fragment(h);
                if(h->is_alive()) play_hazard_hit_sound(Asteroid::explode_sound);
                else              s->score += Hazard::COMET_REWARD;
              } else { ++i; }
            }
          }
          if(!h->is_alive() && station_explode_sound != NULL)
            Mix_PlayChannel(-1, station_explode_sound, 0);
          break;
        }
        case Hazard::SEEKER: {
          // Rams players (lethal unless invincible) and dies to a single shot
          // for a flat bounty. Passes through asteroids like the mini-station.
          if(!h->is_alive()) break;
          bool killed = false;
          for(o = players->begin(); o != players->end() && !killed; o++) {
            Ship* s = (*o)->ship;
            if(s->is_alive() && h->Object::collide(*s)) {
              s->explode(s->position, h->velocity);
              s->kill_stop();
              if(!s->is_alive()) s->detonate();
              h->destroy();
              killed = true;
              break;
            }
            for(size_t i = 0; i < s->bullets.size();) {
              if(h->Object::collide(s->bullets[i])) {
                s->explode(s->bullets[i].position, h->velocity);
                s->bullets[i] = std::move(s->bullets.back());
                s->bullets.pop_back();
                s->score += Hazard::SEEKER_REWARD;
                h->destroy();
                killed = true;
                break;
              } else { ++i; }
            }
            if(killed) break;
            for(size_t i = 0; i < s->missiles.size();) {
              if(h->Object::collide(s->missiles[i])) {
                s->detonate(s->missiles[i].position, s->missiles[i].velocity, Ship::MISSILE_SHRAPNEL);
                if(s->missile_explode_sound != NULL)
                  Mix_PlayChannel(-1, s->missile_explode_sound, 0);
                s->missiles[i] = std::move(s->missiles.back());
                s->missiles.pop_back();
                s->score += Hazard::SEEKER_REWARD;
                h->destroy();
                killed = true;
                break;
              } else { ++i; }
            }
          }
          if(killed && station_explode_sound != NULL)
            Mix_PlayChannel(-1, station_explode_sound, 0);
          break;
        }
      }
    }
    // Reap seekers whose debris burst has faded.
    for(auto hi = hazards->begin(); hi != hazards->end();) {
      if((*hi)->is_removable()) {
        delete *hi;
        hi = hazards->erase(hi);
      } else {
        ++hi;
      }
    }

    /* COLLIDE PLAYERS AND PLAYER SHOTS WITH MINI-STATION */
    // The roaming mini-station dies to a single player shot or to a ram, and
    // rewards a flat bounty. It passes straight through asteroids, so only
    // players and their bullets/missiles interact with it. Ramming destroys
    // both the player and the station, unless the player is invincible, in
    // which case only the station is destroyed.
    if (mini_station != NULL && mini_station->is_alive()) {
      for (o = players->begin(); o != players->end() && mini_station->is_alive(); o++) {
        Ship* s = (*o)->ship;
        // Body collision (ram)
        if (s->is_alive() && mini_station->Object::collide(*s)) {
          s->explode(s->position, mini_station->velocity);
          if (!s->invincible) {
            s->kill_stop();
            s->detonate();
          }
          s->score += GLMiniStation::REWARD;
          mini_station->destroy();
          if (s->is_local_player) {
            Achievements::unlock("mini_station_kill");
            Achievements::progress("score_3m", s->score / 30000);
            if (s->shield_active()) Achievements::unlock("shield_ram");
          } else if (NetPeer *pr = net_peer_for_ship(s)) {
            net_send_event_to(*pr, Net::EV_ACHIEVEMENT,
                              Net::ACH_MINI_STATION_KILL);
            if (s->shield_active())
              net_send_event_to(*pr, Net::EV_ACHIEVEMENT, Net::ACH_SHIELD_RAM);
          }
          break;
        }
        for (size_t i = 0; i < s->bullets.size(); ) {
          if (mini_station->Object::collide(s->bullets[i])) {
            s->explode(s->bullets[i].position, mini_station->velocity);
            s->bullets[i] = std::move(s->bullets.back());
            s->bullets.pop_back();
            s->score += GLMiniStation::REWARD;
            mini_station->destroy();
            if (s->is_local_player) {
              Achievements::unlock("mini_station_kill");
              Achievements::progress("score_3m", s->score / 30000);
            } else if (NetPeer *pr = net_peer_for_ship(s)) {
              net_send_event_to(*pr, Net::EV_ACHIEVEMENT,
                                Net::ACH_MINI_STATION_KILL);
            }
            break;
          } else {
            ++i;
          }
        }
        if (!mini_station->is_alive()) break;
        for (size_t i = 0; i < s->missiles.size(); ) {
          if (mini_station->Object::collide(s->missiles[i])) {
            s->detonate(s->missiles[i].position, s->missiles[i].velocity, Ship::MISSILE_SHRAPNEL);
            if (s->missile_explode_sound != NULL)
              Mix_PlayChannel(-1, s->missile_explode_sound, 0);
            s->missiles[i] = std::move(s->missiles.back());
            s->missiles.pop_back();
            s->score += GLMiniStation::REWARD;
            mini_station->destroy();
            if (s->is_local_player) {
              Achievements::unlock("mini_station_kill");
              Achievements::progress("score_3m", s->score / 30000);
            } else if (NetPeer *pr = net_peer_for_ship(s)) {
              net_send_event_to(*pr, Net::EV_ACHIEVEMENT,
                                Net::ACH_MINI_STATION_KILL);
            }
            break;
          } else {
            ++i;
          }
        }
      }
      // The station was alive when this block started; if a player just
      // destroyed it, play the destruction sound once — attenuated by
      // listener distance, and relayed to the net client.
      if (!mini_station->is_alive()) {
        play_priority_chunk(station_explode_sound,
                            world_volume(mini_station->position));
        net_send_event(Net::EV_STATION_BOOM,
                       Net::pack_pos(mini_station->position.x(),
                                     mini_station->position.y(),
                                     world.x(), world.y()));
      }
    }

    /* APPLY SHOCK-BOLT HITS ON ENEMIES / STATIONS / HAZARDS */
    // Asteroid hits were already applied in each player's collide_grid(); the
    // pointers still in a bolt's `struck` list here are hostiles, damaged with
    // the same APIs bullets/missiles use so scoring and achievements match.
    //
    // The arc only chains onward from a *killing* hit. A target that survives —
    // the station or a comet/pulsar with health left, or a shielded/invincible
    // player — calls bolt.stop() so the lightning ends there instead of arcing
    // past. A one-shot kill (mini-station, seeker, an un-shielded enemy) leaves
    // the bolt growing so it chains to the next-nearest target.
    for(o = players->begin(); o != players->end(); o++) {
      Ship* s = (*o)->ship;
      for(auto &bolt : s->shocks) {
        for(Object *obj : bolt.struck) {
          if(mini_station != NULL && obj == mini_station && mini_station->is_alive()) {
            s->explode(mini_station->position, mini_station->velocity);
            s->score += GLMiniStation::REWARD;
            mini_station->destroy();
            if(s->is_local_player) {
              Achievements::unlock("mini_station_kill");
              Achievements::progress("score_3m", s->score / 30000);
            }
            if(station_explode_sound != NULL)
              Mix_PlayChannel(-1, station_explode_sound, 0);
          } else if(station != NULL && obj == station && station->is_alive()) {
            station->hit();
            if(!station->is_alive()) {
              if(s->is_local_player)
                Achievements::unlock("station_destroyed");
            } else {
              bolt.stop();  // station survived — arc ends here
            }
          } else {
            // Mid-game hazards: a seeker dies to one arc hit (chain onward); a
            // comet/pulsar absorbs several, so the arc stops until it breaks up.
            bool handled = false;
            for(auto* h : *hazards) {
              if(h != obj || !h->is_alive()) continue;
              handled = true;
              if(h->kind_of() == Hazard::SEEKER) {
                h->destroy();
                s->score += Hazard::SEEKER_REWARD;
                if(station_explode_sound != NULL)
                  Mix_PlayChannel(-1, station_explode_sound, 0);
              } else {
                h->hit();
                if(h->kind_of() == Hazard::COMET) {
                  shed_comet_fragment(h);   // knock a couple of chunks loose
                  shed_comet_fragment(h);
                }
                if(h->is_alive()) {
                  play_hazard_hit_sound(h->kind_of() == Hazard::COMET
                                          ? Asteroid::explode_sound
                                          : Asteroid::thud_sound);
                  bolt.stop();  // comet/pulsar survived — arc ends here
                } else {
                  s->score += (h->kind_of() == Hazard::COMET)
                                ? Hazard::COMET_REWARD : Hazard::PULSAR_REWARD;
                  if(station_explode_sound != NULL)
                    Mix_PlayChannel(-1, station_explode_sound, 0);
                }
              }
              break;
            }
            if(handled) continue;

            bool hit = false;
            for(auto* e : *enemies) {
              if(e->ship == obj) {
                s->shock_hit_ship(e->ship);
                if(e->ship->is_alive()) bolt.stop();  // survived (invincible) — arc ends
                hit = true; break;
              }
            }
            // Friendly fire: a bolt that arced to the other player damages it,
            // credited exactly like a bullet (score, no enemy achievement). A
            // shielded/invincible player survives, so the arc stops at them.
            if(!hit && friendly_fire) {
              for(auto* p : *players) {
                if(p->ship == obj && p->ship != s) {
                  s->shock_hit_ship(p->ship);
                  if(p->ship->is_alive()) bolt.stop();
                  break;
                }
              }
            }
          }
        }
        bolt.struck.clear();
      }
    }

    /* KILL-ALIGNED OBJECTS VS DEPLOYED TURRETS */
    // A turret dies to anything that could kill a ship: hostile bullets
    // (enemies', the mini-station's, the other player's under friendly
    // fire), a comet or seeker ram, the mini-station's hull. Asteroid
    // contact is handled in each owner's collide_grid pass (which owns the
    // grid) and expiry in Ship::step. Debris only — no blast, no bounty.
    for (auto *tgs : *players) {
      Ship *ts = tgs->ship;
      for (size_t ti = 0; ti < ts->turrets.size(); ) {
        TurretDrone &t = ts->turrets[ti];
        bool t_dead = false;
        for (auto *h : *hazards) {
          if (!h->is_alive()) continue;
          if (h->kind_of() != Hazard::COMET && h->kind_of() != Hazard::SEEKER)
            continue;  // a pulsar's body shoves, it doesn't kill
          if (h->Object::collide(t)) { t_dead = true; break; }
        }
        if (!t_dead && mini_station != NULL && mini_station->is_alive() &&
            mini_station->Object::collide(t))
          t_dead = true;
        if (!t_dead && mini_station != NULL) {
          for (size_t i = 0; i < mini_station->bullets.size(); i++) {
            if (t.collide(mini_station->bullets[i])) {
              mini_station->bullets[i] = std::move(mini_station->bullets.back());
              mini_station->bullets.pop_back();
              t_dead = true;
              break;
            }
          }
        }
        if (!t_dead) {
          for (auto *e : *enemies) {
            Ship *es = e->ship;
            for (size_t i = 0; i < es->bullets.size(); i++) {
              if (t.collide(es->bullets[i])) {
                es->bullets[i] = std::move(es->bullets.back());
                es->bullets.pop_back();
                t_dead = true;
                break;
              }
            }
            if (t_dead) break;
          }
        }
        // The partner's fire only counts when friendly fire is on (with it
        // off their bullets can't hurt the owner either). Never the
        // owner's own — its shots leave from this turret's muzzle.
        if (!t_dead && friendly_fire) {
          for (auto *p2 : *players) {
            Ship *s2 = p2->ship;
            if (s2 == ts) continue;
            for (size_t i = 0; i < s2->bullets.size(); i++) {
              if (t.collide(s2->bullets[i])) {
                s2->bullets[i] = std::move(s2->bullets.back());
                s2->bullets.pop_back();
                t_dead = true;
                break;
              }
            }
            if (t_dead) break;
          }
        }
        if (t_dead) {
          ts->turret_explode(t);
          ts->turrets[ti] = std::move(ts->turrets.back());
          ts->turrets.pop_back();
        } else {
          ++ti;
        }
      }
    }

    // Remove the mini-station once destroyed and its debris/bullets have faded.
    if (mini_station != NULL && mini_station->is_removable()) {
      delete mini_station;
      mini_station = NULL;
    }

    o = enemies->begin();
    while(o != enemies->end()) {
      if((*o)->is_removable()) {
        ship_objects->remove((*o)->ship);
        delete *o;
        o = enemies->erase(o);
      } else {
        o++;
      }
    }

    // Big station death boom, fired on the alive→dead transition so every
    // kill path (bullets, missiles, ramming, lance, net kill claims) plays
    // it exactly once — attenuated by listener distance, and relayed to the
    // net client (whose replica runs the debris burst in restore_state).
    {
      bool station_alive_now = station != NULL && station->is_alive();
      if (station_alive_prev && !station_alive_now && station != NULL) {
        play_priority_chunk(station_explode_sound,
                            world_volume(station->position));
        net_send_event(Net::EV_STATION_BOOM,
                       Net::pack_pos(station->position.x(),
                                     station->position.y(),
                                     world.x(), world.y()));
        NET_LOG("station destroyed at (%.0f, %.0f)",
                station->position.x(), station->position.y());
      }
      station_alive_prev = station_alive_now;
    }

    // Remove dead station once its debris has faded and all its ships are gone.
    if (station != NULL && station->is_removable() && enemies->empty()) {
      delete station;
      station = NULL;
    }

    /* COLLIDE PICKUPS WITH PLAYERS */
    for(o = players->begin(); o != players->end(); o++) {
      if(!(*o)->ship->is_alive()) continue;
      for(auto pi = pickups->begin(); pi != pickups->end(); pi++) {
        if(!(*pi)->collected && (*pi)->collide(*(*o)->ship)) {
          (*pi)->collected = true;
          (*pi)->apply((*o)->ship);
          // The revive targets the fallen PARTNER (the pickup can't see the
          // player list — same pattern as the mini-station reward): put them
          // back on their last life and let the ordinary respawn machinery
          // run. lives=1 restarts the parked countdown; the respawn charges
          // the life back to 0 = the standard alive-on-last-life state.
          // Online this is host-side sim; lives/alive replicate, the client's
          // spectate ends by itself, and their alive-transition respawns the
          // wreck. A revive with nobody down (partner beat it back some other
          // way) is just the pickup sound.
          if (dynamic_cast<RevivePickup*>(*pi))
            revive_fallen_partner((*o)->ship);
          // The time-slow clock is a world effect like the revive: the
          // pickup can't see the game's step clock, so GLGame starts it at
          // the collection site.
          if (dynamic_cast<TimeSlowPickup*>(*pi))
            start_time_slow((*o)->ship);
          if(pickup_sound != NULL)
            Mix_PlayChannel(-1, pickup_sound, 0);
          // Collection cue for the client. The arg names WHICH of the
          // CLIENT's limited primaries gained ammo (NetPrimaryKind) so the
          // client arms exactly that weapon's anti-flicker latch — 0 for the
          // host's own pickups and for types that touch no primary ammo (see
          // net_pickup_latch_ in glgame.h for why kind-blind arming misfired).
          // B4: the latch kind goes ONLY to the collecting peer — arming
          // another client's latch would pin its real pickup gains low.
          // Everyone else still hears the collection cue (arg 0). At one
          // peer this is byte- and tee-identical to the old broadcast.
          NetPeer *collector = NULL;
          if (net_mode_ == NetHost)
            for (NetPeer *pr : net_peers_) {
              GLShip *gs = player_by_seat(pr->seat);
              if (gs && gs == (*o)) { collector = pr; break; }
            }
          if (collector) {
            uint32_t pk = NET_PRIM_NONE;
            if (dynamic_cast<BeamPickup*>(*pi))         pk = NET_PRIM_BEAM;
            else if (dynamic_cast<LancePickup*>(*pi))   pk = NET_PRIM_LANCE;
            else if (dynamic_cast<ShockPickup*>(*pi))   pk = NET_PRIM_SHOCK;
            else if (dynamic_cast<GodModePickup*>(*pi)) pk = NET_PRIM_GOD;
            net_send_event_to(*collector, Net::EV_PICKUP, pk);
            for (NetPeer *pr : net_peers_)
              if (pr != collector)
                net_send_event_to(*pr, Net::EV_PICKUP, NET_PRIM_NONE,
                                  /*tee=*/false);
          } else {
            net_send_event(Net::EV_PICKUP, NET_PRIM_NONE);
          }
        }
      }
    }

    /* STEP AND CLEAN UP PICKUPS */
    auto pi = pickups->begin();
    while(pi != pickups->end()) {
      if((*pi)->is_removable()) {
        delete *pi;
        pi = pickups->erase(pi);
      } else {
        (*pi)->step(step_size);
        pi++;
      }
    }

    // Time-slow pickup: count down in SIM ms (so pause and the intro freeze
    // it), and while active schedule the next step kTimeSlowFactor times
    // further apart — the sim still advances step_size of game time per
    // step, so nothing in-game changes rate; the wall clock just watches it
    // in slow motion, except the collector's compensated turning. Online
    // the countdown rides every snapshot and the client mirrors this exact
    // pacing in tick_net_client, so both machines slow in lockstep
    // (PROTO 24).
    time_slow_step();
    time_until_next_step += time_between_steps *
        (time_slow_active() ? kTimeSlowFactor : 1);
  }
  /* Save high score automatically on game over */
  if (!game_over && !players->empty()) {
    bool all_game_over = true;
    for (auto* glship : *players) {
      if (glship->ship->is_alive() || glship->ship->lives > 0) {
        all_game_over = false;
        break;
      }
    }
    if (all_game_over) {
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      game_over = true;
      Stats::flush();
      // The run is over: patch the replay header (accurate final
      // score/generation) and retire the file — offline rotates current
      // -> recent, online best-checks the ONLINE RUN slot in place.
      replay_finish(true);
      board_maybe_start();  // new personal best -> leaderboard prompt
      // Host: finalize's forced final keyframe rebuilt the shared delta
      // baseline without being SENT, so an asteroid spawned since the last
      // send could go missing client-side for up to a second (until the
      // 1 Hz keyframe) on the game-over screen. Send a fresh keyframe next
      // slot to re-anchor the client immediately.
      if (net_mode_ == NetHost) net_force_keyframe_ = true;
      score_saved = true;
      game_over_time = current_time;
#ifdef __EMSCRIPTEN__
      // Show the tap-to-continue overlay so any touch reaches _web_tap_start().
      EM_ASM(if (window.setMenuMode) window.setMenuMode(1););
      if (net_mode_ != NetReplay) web_notify_game_over(players);
#endif
    }
  }

  // Arm/advance the spectate countdown (host side): local player out, peer
  // still in it -> after kSpectateDelayMs the camera follows the peer.
  update_spectate();

  /* Delete save on true game over */
  if (game_over && !save_deleted_ && net_mode_ == NetOff) {
    Save::delete_save();
    save_deleted_ = true;
  }

  /* Save on death while lives remain (once per death window) */
  if (!game_over && net_mode_ == NetOff && !players->empty()) {
    bool any_dead_with_lives = false;
    bool any_dead_no_lives   = false;
    for (auto* glship : *players) {
      if (!glship->ship->is_alive()) {
        if (glship->ship->lives > 0) any_dead_with_lives = true;
        else                         any_dead_no_lives   = true;
      }
    }
    // score_saved doubles as the "never persist" latch the screenshot
    // harness sets (shot_scene.h) — this direct write must respect it like
    // every save_progress path does.
    if (any_dead_with_lives && !any_dead_no_lives && !save_written_this_death_ &&
        !score_saved) {
      Save::save_game(build_save_data());
      save_written_this_death_ = true;
    }
    if (!any_dead_with_lives)
      save_written_this_death_ = false;
  }

  // Online, the shield hum is a personal cue (your invincibility window /
  // shield). The remote player's ship respawns hum through Ship::respawn
  // exactly like a local split-screen partner's would — but online that
  // ship is usually a world away on someone else's screen, so the short
  // full-volume hum at every remote respawn just sounds random. Mute it.
  if (net_mode_ == NetHost && players->size() >= 2) {
    // (The sim would play the remote players' own sounds like a
    // split-screen partner's — full volume. update_player_sound_volumes,
    // in the step loop above, attenuates them by distance instead.)
    // B5: per SEAT — the hum mute covers every remote hull, and the
    // parked-shield re-assert follows the PARKED peer's hull only. The
    // old back() form set a 6-day invincibility on whichever ship
    // happened to be last — under play-on that is a HEALTHY player.
    for (NetPeer *pw : net_peers_) {
      GLShip *gs = player_by_seat(pw->seat);
      if (!gs) continue;
      gs->ship->set_shield_hum(false);
      // While parked for rejoin, re-assert the shield every tick: level
      // rebuilds respawn all players with the normal 1.5 s window, which
      // would otherwise expire and leave the pilotless hull killable.
      if (pw->parked && net_signal_ && gs->ship->is_alive()) {
        gs->ship->invincible = true;
        gs->ship->time_left_invincible = 1 << 29;
      }
    }
  }

  // (Bullet-vs-asteroid impact cues used to be forwarded here as
  // EV_ROID_THUD/TING events; since PROTO 10 the client detects those
  // cosmetics locally — Ship::net_cosmetic_impacts.)
  //
  // The EVENT drains run for the online host AND for any live recording:
  // net_send_event tees the event into the replay file, then returns
  // without a session — so offline replays get the enemy-death booms and
  // ship-bounce debris/ting cues the net client gets. (They used to sit
  // behind the host gate, which left offline playback silent on both:
  // playback ghosts never run the collision sim that plays them live.)
  // The MSG echo loops below stay host-gated — raw transport writes, and
  // every one already has a recorded twin (outbox or receive tee).
  // B5 play-on: one lost seat must not stop the cosmetic event flow to
  // the peers still playing — only an empty roster does.
  bool host_online =
      net_mode_ == NetHost && net_session() && !net_all_peers_lost();
  if (host_online || replay_) {
    // Non-fatal ship-vs-asteroid bounces (debris + armour ting). Enemy
    // ships collide through the same code; only player ships are sent.
    // PROTO 25: the arg's low byte is the ship's seat (only player ships
    // carry one — an enemy's impact stays local, as before).
    for (const Ship::NetShipImpact &si : Ship::net_ship_impacts) {
      uint32_t idx = si.ship->net_seat;
      if (idx)
        net_send_event(Net::EV_SHIP_IMPACT, idx | (si.ting ? 0x100u : 0u));
    }
    // Shots: world actors' (enemies, the mini-station) go over as
    // EV_WORLD_SHOT with their position for attenuation. The HOST
    // player's are superseded by the MSG_SHOT echo below (PROTO 17),
    // whose clone spawn plays the shot sound client-side; the client
    // fires its own weapon locally, and the host simulates the client's
    // shots too — neither is relayed. (Never recorded — the tee skips
    // EV_WORLD_SHOT; FX_SHOT from the replay_pews outbox is its twin.)
    // B4: "a world actor" = not ANY player's ship (was a front/back pair test).
    auto is_player_ship = [this](const Ship *s) {
      for (auto *gs : *players)
        if (gs->ship == s) return true;
      return false;
    };
    for (const Ship *shooter : Ship::net_shots) {
      if (!is_player_ship(shooter))
        net_send_event(Net::EV_WORLD_SHOT,
                       Net::pack_pos(shooter->position.x(),
                                     shooter->position.y(),
                                     world.x(), world.y()));
    }
    // Deaths: player explosions already reach the client through the
    // snapshot extras; world actors' need the event.
    for (const Ship *boom : Ship::net_booms)
      if (!is_player_ship(boom))
        net_send_event(Net::EV_WORLD_BOOM,
                       Net::pack_pos(boom->position.x(), boom->position.y(), world.x(), world.y()));
  }
  if (host_online) {
    // B4: every echo below is a byte-identical broadcast to the live peers.
    auto send_all = [this](const std::vector<uint8_t> &m) {
      for (NetPeer *p : net_peers_)
        if (p->session && !p->lost)
          p->session->transport()->send_reliable(&m[0], m.size());
    };
    // PROTO 17: echo the host player's shots as MSG_SHOT (the mirror of
    // the client's PROTO 14 reports; p1 is the only reporter on a host).
    // The client spawns exact clones instantly — without this a host
    // bullet exists there only via the 10 Hz snapshot rebuild, popping in
    // up to a snapshot interval late and already down-range.
    // PROTO 25: the header stamps the FIRER's seat (p1 = 1 — it used to
    // stamp literal 2, proof nothing read it; the receivers resolve the
    // firing ship by this byte now, which is what lets B4 relay peer
    // effects with attribution).
    for (const Ship::NetShotReport &r : Ship::net_shot_reports) {
      std::vector<uint8_t> msg;
      Net::put_header(msg, Net::MSG_SHOT, 1);
      Net::put_u32(msg, r.id);
      Net::put_f32(msg, r.x);
      Net::put_f32(msg, r.y);
      Net::put_f32(msg, r.vx);
      Net::put_f32(msg, r.vy);
      Net::put_u8(msg, (r.kills_invincible ? 1 : 0) | (r.has_trail ? 2 : 0) |
                       (r.piercing ? 4 : 0));
      send_all(msg);
    }
    // PROTO 18: the host player's lance pulses, echoed for the client's
    // flash + sound (the kills replicate as ordinary removal records).
    for (const auto &rep : Ship::net_lance_reports) {
      const std::vector<Point> &pts = rep.second;
      if (pts.size() < 2 || pts.size() > 17) continue;
      std::vector<uint8_t> msg;
      Net::put_header(msg, Net::MSG_LANCE,
                      rep.first && rep.first->net_seat ? rep.first->net_seat
                                                       : 1);
      Net::put_u8(msg, (uint8_t)pts.size());
      for (const Point &p : pts) {
        Net::put_f32(msg, p.x());
        Net::put_f32(msg, p.y());
      }
      send_all(msg);
    }
    // PROTO 22: the host player's shock bolts, echoed for the client's
    // flash + sound (kills replicate as ordinary removal / score records).
    for (const auto &rep : Ship::net_shock_reports) {
      const std::vector<Point> &pts = rep.second;
      if (pts.size() < 2 || pts.size() > 15) continue;
      std::vector<uint8_t> msg;
      Net::put_header(msg, Net::MSG_SHOCK,
                      rep.first && rep.first->net_seat ? rep.first->net_seat
                                                       : 1);
      Net::put_u8(msg, (uint8_t)pts.size());
      for (const Point &p : pts) {
        Net::put_f32(msg, p.x());
        Net::put_f32(msg, p.y());
      }
      send_all(msg);
    }
    // PROTO 19: authoritative ricochets — the sim's real bounce of any
    // id-carrying bullet overrides the client's local approximation, so
    // both screens fly the same post-bounce trajectory.
    // The bounced bullet's id already names its firing seat (top nibble);
    // the header carries the sender's seat like every host message.
    for (const Ship::NetBounceReport &r : Ship::net_bounce_reports) {
      std::vector<uint8_t> msg;
      Net::put_header(msg, Net::MSG_BOUNCE, 1);
      Net::put_u32(msg, r.id);
      Net::put_f32(msg, r.x);
      Net::put_f32(msg, r.y);
      Net::put_f32(msg, r.vx);
      Net::put_f32(msg, r.vy);
      Net::put_u8(msg, r.flags);
      send_all(msg);
    }
  }
  Ship::net_ship_impacts.clear();
  Ship::net_shots.clear();
  Ship::net_booms.clear();
  // Cleared outside the session guard: with no live session (solo online
  // after a leave, rejoin pending) the queues must not grow unbounded.
  Ship::net_shot_reports.clear();
  Ship::net_lance_reports.clear();
  Ship::net_shock_reports.clear();
  Ship::net_bounce_reports.clear();
  // Achievement relays: unlocks the sim attributed to a remote replica
  // (ram kills resolve inside Ship code). Targeted (B4) to the peer whose
  // ship earned it; anything not a peer's — an enemy, or offline residue —
  // is dropped.
  for (const auto &ar : Ship::net_ach_relays)
    for (NetPeer *pr : net_peers_) {
      GLShip *gs = player_by_seat(pr->seat);
      if (gs && ar.first == gs->ship) {
        net_send_event_to(*pr, Net::EV_ACHIEVEMENT, ar.second);
        break;
      }
    }
  Ship::net_ach_relays.clear();
  // Shield-ram bursts the sim minted into a player's bullets: only a
  // remote replica's need the wire (the client skips its own-ship bullet
  // echo); the host's own replicate through the ordinary echo. Targeted
  // to the ramming peer; the loss guard keeps the old call's tee gating.
  for (const Ship *rb : Ship::net_ram_blasts)
    if (net_mode_ == NetHost)
      for (NetPeer *pr : net_peers_) {
        GLShip *gs = player_by_seat(pr->seat);
        if (gs && rb == gs->ship) {
          if (!pr->lost) net_send_event_to(*pr, Net::EV_RAM_BLAST);
          break;
        }
      }
  Ship::net_ram_blasts.clear();

  // Online host: broadcast the world at 10 Hz once everything has stepped —
  // the recorder tees the built payloads from inside that path. Offline,
  // the replay recorder consumes the same builders at the same cadence
  // (either way this point is only reached while running, so pauses record
  // nothing); it must never run alongside the host path — a second build
  // per slot would corrupt the shared delta baseline. Effect flashes drain
  // every tick, BEFORE the slot record so they attach to the slot about to
  // be written — they are one-shot mints, not 10 Hz state — recorded when
  // a recorder is live and discarded otherwise (the host's own mints
  // included; the statics never grow).
  replay_drain_effects();
  if (net_mode_ == NetHost) net_host_send_snapshot(delta);
  else if (replay_) replay_record_slot(delta);

  /* Display FPS */
  //std::cout << (num_frames*1000 / current_time) << std::endl;
}

void GLGame::draw_objects(float direction, bool minimap,
                          float cam_x, float cam_y, float cull_r) const {
  if(debug_grid && !minimap) grid.draw_debug();

  for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
    (*bhi)->draw(minimap);
  }

  for(auto* h : *hazards) {
    h->draw(minimap);
  }

  AsteroidDrawer::draw_batch(objects, dead_objects, direction, minimap,
                             minimap ? world.x() : 0, minimap ? world.y() : 0,
                             cam_x, cam_y, cull_r);

  for(auto pi = pickups->begin(); pi != pickups->end(); pi++) {
    if (cull_r > 0) {
      // Glow halos extend past the pickup radius — cull generously.
      float reach = cull_r + (*pi)->radius * 4.0f;
      float rx = (*pi)->position.x() - cam_x;
      float ry = (*pi)->position.y() - cam_y;
      if (rx*rx + ry*ry > reach*reach) continue;
    }
    (*pi)->draw(direction);
  }

  std::list<GLShip*>::iterator o;
  for(o = players->begin(); o != players->end(); o++) {
    (*o)->draw(minimap);
  }
  for(o = enemies->begin(); o != enemies->end(); o++) {
    (*o)->draw(minimap);
  }

  if(station != NULL) station->draw(minimap);
  if(mini_station != NULL) mini_station->draw(minimap);
}

void GLGame::draw(void) {
  PerfScope perf_scope_(&perf_draw_ms_, &perf_draw_max_);
  perf_report();  // once per second, only while below ~55 fps
  // Camera smoothing advances by SIMULATED time, not by how long the last
  // frame took to draw. Those are the same thing while the game runs at
  // speed, and nothing like it otherwise: under the time-scale debug keys or
  // a replay's 4x fast-forward the world moved at one rate and the camera
  // chased at another, and in an offline video render (shots/video.sh), where
  // a 1080p frame can take a tenth of a second on software GL, a wall clock
  // made every frame's smoothing step enormous — the captured camera snapped
  // where the played camera glides. Banked in tick() so a draw that skips a
  // tick (or a tick with no draw) still gets exactly the time that passed.
  int frame_delta = camera_delta_pending_;
  camera_delta_pending_ = 0;
  // Hitch breakdown, draw half (see the "slow tick" line): a slow frame
  // that ISN'T in poll/steps is render-side.
  uint32_t hb_draw_t0 = SDL_GetTicks();
  for(GLShip *gs : *players) gs->smooth_camera(frame_delta);

  glClear(GL_COLOR_BUFFER_BIT /*| GL_DEPTH_BUFFER_BIT*/);

  if(players->size() == 0) {
    draw_world();
  }
  else if(net_mode_ == NetReplay) {
    // Replay playback: the same viewports the game showed while being
    // played — one full view for a solo run, the split layout for a co-op
    // run, each viewport following its own ghost (REPLAY.md R2).
    int vp = 0;
    for (GLShip *gs : *players) draw_world(gs, vp++);
    if (!shot_hide_hud_) draw_map();
    Overlay::net_overlays(this);  // generation banner + shared GAME OVER card
    if (!replay_hide_chrome_) Overlay::replay_hud(this);
  }
  else if(net_mode_ != NetOff) {
    // Online: one full-screen view following the local player (host =
    // front of the list, client = back). Once the local player is fully
    // out and spectating has begun, the camera hands off to the peer. The
    // peer draws their own view.
    draw_world(camera_target(), 0);
    draw_map();
    Overlay::net_overlays(this);
  }
  else {
    int vp = 0;
    for (GLShip *gs : *players) draw_world(gs, vp++);
    //Draw map after - for partial translucency
    if (!shot_hide_hud_) draw_map();
  }
  // Pause overlay — full-window, after every viewport pass, so split-screen
  // shows ONE menu instead of a copy per cell (it answers one shared
  // cursor). No-op while the game runs.
  Overlay::paused(this);
  Overlay::seat_roster(this);  // replaces the pause menu while it is open
  // Leaderboard prompt/upload/result — its own full-window overlay so the
  // OFFLINE game-over card gets it too (the primary solo case). No-op
  // unless a board flow is live (LEADERBOARD.md).
  Overlay::board_prompt(this);
  if (net_mode_ != NetOff && Net::net_debug_enabled()) {
    uint32_t draw_ms = SDL_GetTicks() - hb_draw_t0;
    if (draw_ms > 25) NET_LOG("net: slow draw %u ms\n", draw_ms);
  }
}


int GLGame::num_x_viewports() const {
  // Online each machine draws one view; a replay reproduces the recorded
  // game's own layout, like offline. 2 players split along the window's
  // long axis; 3-4 players use the 2x2 grid in either orientation (D4).
  if (net_mode_ == NetHost || net_mode_ == NetClient) return 1;
  size_t n = players->size();
  if (n <= 1) return 1;
  if (n >= 3) return 2;
  return (window.x() > window.y()) ? 2 : 1;
}

int GLGame::num_y_viewports() const {
  if (net_mode_ == NetHost || net_mode_ == NetClient) return 1;
  size_t n = players->size();
  if (n <= 1) return 1;
  if (n >= 3) return 2;
  return (window.x() > window.y()) ? 1 : 2;
}

bool GLGame::is_visible_to_any_player(const Ship &ship) const {
  for(auto* glship : *players) {
    if(!glship->ship->is_alive()) continue;
    float fov_deg = glship->view_angle();
    float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
    float aspect = (window.x() / (float)num_x_viewports()) /
                   (window.y() / (float)num_y_viewports());
    float half_w = half_h * aspect;
    float cull_r2 = (half_w * half_w + half_h * half_h) * 1.1f;
    float dist = glship->ship->position.distance_to(ship.position);
    if(dist * dist <= cull_r2) return true;
  }
  return false;
}

bool GLGame::is_visible_to_any_player(Point p) const {
  for(auto* glship : *players) {
    if(!glship->ship->is_alive()) continue;
    float fov_deg = glship->view_angle();
    float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
    float aspect = (window.x() / (float)num_x_viewports()) /
                   (window.y() / (float)num_y_viewports());
    float half_w = half_h * aspect;
    float cull_r2 = (half_w * half_w + half_h * half_h) * 1.1f;
    float dist = glship->ship->position.distance_to(p);
    if(dist * dist <= cull_r2) return true;
  }
  return false;
}

// Online: the only listener is the local player. The split-screen
// variant below treats EVERY player as a listener, which puts the remote
// ship at distance zero from its own sounds.
// PROTO 18: a peer's lance pulse — display-only. The kills travel on their
// own channel (bullet_id-0 MSG_HIT claims from a client firer, ordinary
// removal records from the host); this is the flash and the sound.
bool GLGame::net_receive_lance_pulse(Net::Reader &r, Ship *shooter) {
  uint8_t n = r.u8();
  if (!r.ok || n < 2 || n > 17) return false;
  LancePulse pulse;
  pulse.ttl = pulse.time_left = 250.0f;
  for (int i = 0; i < n; i++) {
    float x = r.f32(), y = r.f32();
    if (!r.ok || !std::isfinite(x) || !std::isfinite(y)) return false;
    pulse.points.push_back(Point(x, y));
  }
  // world_volume, not net_listener_volume: online they are the same call
  // (all_players_local() is false, so the local camera is the only listener),
  // but a REPLAY is split-screen — every ghost has a viewport, and CLAUDE.md
  // puts that distinction in exactly one place. Keyed to the local camera,
  // a lance was silent whenever player 1's ghost happened to be in a respawn
  // countdown, and player 2's bolt was attenuated against player 1's screen.
  float vol = world_volume(pulse.points.front());
  if (vol > 0.0f) {
    static Mix_Chunk *lance_snd =
        Mix_LoadWAV(asset_path("audio/lance.wav").c_str());
    if (lance_snd) {
      Mix_VolumeChunk(lance_snd, (int)(MIX_MAX_VOLUME * vol));
      Mix_PlayChannel(-1, lance_snd, 0);
    }
  }
  shooter->lance_pulses.push_back(std::move(pulse));
  NET_LOG("net: lance pulse received (%d points)\n", (int)n);
  return true;
}

// PROTO 22: parse a shock-bolt polyline, show it on the firer's replica (the
// exact segments, never re-seeked) with the zap sound, and hand the points
// back so the host can resolve station/mini hull damage from them.
bool GLGame::net_receive_shock_pulse(Net::Reader &r, Ship *shooter,
                                     std::vector<Point> *out) {
  uint8_t n = r.u8();
  if (!r.ok || n < 2 || n > 15) return false;
  std::vector<Point> pts;
  for (int i = 0; i < n; i++) {
    float x = r.f32(), y = r.f32();
    if (!r.ok || !std::isfinite(x) || !std::isfinite(y)) return false;
    pts.push_back(Point(x, y));
  }
  float vol = world_volume(pts.front());  // see net_receive_lance_pulse
  if (vol > 0.0f) {
    static Mix_Chunk *shock_snd =
        Mix_LoadWAV(asset_path("audio/shock.wav").c_str());
    if (shock_snd) {
      Mix_VolumeChunk(shock_snd, (int)(MIX_MAX_VOLUME * vol));
      Mix_PlayChannel(-1, shock_snd, 0);
    }
  }
  shooter->net_receive_shock(pts);
  if (out) *out = pts;
  NET_LOG("net: shock bolt received (%d points)\n", (int)n);
  return true;
}

// True if the client's shock polyline STRUCK this body. When grow_segment
// registers a hit it snaps the vertex onto the target's surface (surface_hit:
// exactly `radius` from the centre for circle bodies — the raw f32 polyline
// rides the wire unquantized), so a genuine hit always has a vertex at
// distance <= radius + epsilon. Testing radius + HIT_RADIUS here (the seek
// slack) instead made the host damage BYSTANDERS: any hazard/station/partner
// a meandering vertex merely passed within ~16 units of took hull damage and
// paid score the firing client never showed.
static bool shock_bolt_reaches(const std::vector<Point> &pts,
                               const WrappedPoint &pos, float radius) {
  float r = radius + 2.0f;  // surface-snap epsilon, not the seek slack
  for (const Point &p : pts) {
    Point c = pos.closest_to(p);
    float dx = c.x() - p.x(), dy = c.y() - p.y();
    if (dx * dx + dy * dy <= r * r) return true;
  }
  return false;
}

// Half-diagonal, in world units, of what one camera can see. Anything nearer
// than this MIGHT be on screen; anything further certainly is not.
static float camera_screen_radius(float fov_deg, Point window, int x_viewports,
                                  int y_viewports) {
  float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
  float half_w = half_h * ((window.x() / (float)x_viewports) /
                           (window.y() / (float)y_viewports));
  return sqrtf(half_w * half_w + half_h * half_h);
}

// How loud a source `dist` away is to a camera that can see `screen_r` around
// itself: FULL VOLUME while it could still be on screen, then a linear fade
// to silence over another screen-radius past the edge.
//
// The fade band is what makes the plateau safe to have — cutting out at the
// edge instead would pop every sound off as it left the view. And the plateau
// is the point: falling off from the CENTRE (what this did at first) quietened
// the whole game, because an asteroid dying at arm's length in front of you is
// already a good fraction of a screen away.
static const float kAudibleScreenRadii = 2.0f;

static float listener_falloff(float dist, float screen_r) {
  if (screen_r <= 0.0f) return 0.0f;
  if (dist <= screen_r) return 1.0f;
  float fade = screen_r * (kAudibleScreenRadii - 1.0f);
  float t = (dist - screen_r) / fade;
  return t >= 1.0f ? 0.0f : 1.0f - t;
}

float GLGame::net_listener_volume(Point p) const {
  // The listener is whoever the camera follows: normally the local player,
  // but the peer while spectating — the spectator hears the action they
  // are watching (their own wreck is dead and would mute everything).
  GLShip *me = camera_target();
  if (!me || !me->ship->is_alive()) return 0.0f;
  // Online is always one full-window viewport (the peer is on their own box).
  return listener_falloff(me->ship->position.distance_to(p),
                          camera_screen_radius(me->view_angle(), window, 1, 1));
}

bool GLGame::all_players_local() const {
  // Split-screen shows every player on this screen, so every player is a
  // listener. That covers an offline 2P game and a 2P replay (both ghosts
  // get a viewport) — live online is the case where the peer is somewhere
  // else entirely and only the local camera has ears.
  return net_mode_ == NetOff || net_mode_ == NetReplay;
}

float GLGame::world_volume(Point p) const {
  return all_players_local() ? sound_volume_for_point(p) : net_listener_volume(p);
}

// Per-tick listener bookkeeping for the player ships. Only the sounds a ship
// plays for itself are covered here (gun, thrusters, explosion, respawn tics)
// — Ship multiplies them all by sound_volume_scale.
//
// The local player always hears itself at full volume, dead or alive: its
// respawn tics and god-mode music are cues about YOUR ship, not sounds
// arriving from somewhere in the world. The peer is a sound source like any
// other and fades with distance to the camera. Offline (and in a split-screen
// replay) nobody fades — both players are watching the same screen.
//
// Re-run every tick rather than on control changes: the thruster hum is a
// looping channel whose level is chunk volume, and the distance behind that
// level keeps moving while a thrust key is simply held down.
void GLGame::update_player_sound_volumes() {
  GLShip *local = local_player();
  for (auto *gs : *players) {
    Ship *s = gs->ship;
    bool mine = all_players_local() || gs == local;
    s->sound_volume_scale = mine ? 1.0f : net_listener_volume(s->position);
    s->sound_own_cues = mine;
    s->update_boost_volume();
    s->update_missile_fly_volumes();
  }
}

float GLGame::sound_volume_for_point(Point p) const {
  float best = 0.0f;
  for(auto* glship : *players) {
    if(!glship->ship->is_alive()) continue;
    // Split-screen: each player's viewport is a fraction of the window.
    float v = listener_falloff(
        glship->ship->position.distance_to(p),
        camera_screen_radius(glship->view_angle(), window,
                             num_x_viewports(), num_y_viewports()));
    if(v > best) best = v;
  }
  return best;
}

bool GLGame::is_point_faced_by_any_player(Point p) const {
  for(auto* glship : *players) {
    Ship *s = glship->ship;
    if(!s->is_alive()) continue;
    // Get wrapped vector from ship to point
    Point ship_near = s->position.closest_to(p);
    float dx = p.x() - ship_near.x();
    float dy = p.y() - ship_near.y();
    // Project into the ship's facing frame:
    //   fwd  = component along facing direction (positive = in front)
    //   side = component along the right perpendicular of facing
    float fwd  = dx * s->facing.x() + dy * s->facing.y();
    float side = dx * s->facing.y() - dy * s->facing.x();
    // Viewport rectangle in world units (camera at z=1000, FOV-derived)
    float fov_deg = glship->view_angle();
    float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
    float aspect = (window.x() / (float)num_x_viewports()) /
                   (window.y() / (float)num_y_viewports());
    float half_w = half_h * aspect;
    // Reject if outside the on-screen rectangle
    if(fwd <= 0.0f || fwd > half_h || fabsf(side) > half_w) continue;
    // Facing cone: ±45° from ship heading
    float len = sqrtf(fwd * fwd + side * side);
    if(len < 1e-6f) return true;
    if(fwd / len > 0.7071f) return true;
  }
  return false;
}


GLGame::ViewportRect GLGame::viewport_rect(int vp_index) const {
  int nx = num_x_viewports(), ny = num_y_viewports();
  int w = window.x() / nx, h = window.y() / ny;
  // Column left-to-right; row TOP-down (player 1 top-left), flipped to
  // GL's bottom-left viewport origin.
  int col = vp_index % nx;
  int row = vp_index / nx;
  if (row >= ny) row = ny - 1;  // paranoia: never place outside the window
  ViewportRect r;
  r.x = col * w;
  r.y = (ny - 1 - row) * h;
  r.w = w;
  r.h = h;
  return r;
}

void GLGame::setup_viewport(int vp_index) const {
  ViewportRect r = viewport_rect(vp_index);
  glViewport(r.x, r.y, r.w, r.h);
}

void GLGame::draw_world(GLShip *glship, int vp_index) const {
  float nx = (float)num_x_viewports();
  float ny = (float)num_y_viewports();
  float fovy = (glship == NULL) ? 85.0f
             : (ny == 1.0f) ? glship->view_angle()
             : glship->view_angle() * 0.75f;
  float aspect = (window.x() / nx) / (window.y() / ny);
  float proj[16]; mat4_perspective(proj, fovy, aspect, 100.0f, 2000.0f);
  float view[16]; mat4_lookat(view, 0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
  float pv[16]; mat4_mul(pv, proj, view);
  gles2_set_vp(pv);
  setup_viewport(vp_index);
  draw_perspective(glship);

  float osd_hw = window.x() / nx;
  float osd_hh = window.y() / ny;
  const float MAX_OSD_ASPECT = 16.0f / 9.0f;
  float capped_hw = (osd_hw / osd_hh > MAX_OSD_ASPECT) ? osd_hh * MAX_OSD_ASPECT : osd_hw;

  // Widening the ortho extents shrinks all HUD content uniformly into the
  // title-safe region (no-op where SAFE_AREA_SCALE is 1.0).
  float safe_hw = osd_hw / Overlay::SAFE_AREA_SCALE;
  float safe_hh = osd_hh / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -safe_hw, safe_hw, -safe_hh, safe_hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);
  setup_viewport(vp_index);
  float saved_sw = Typer::scaled_window_width;
  Typer::scaled_window_width = capped_hw / Typer::scale * nx;
  Uint64 pc0 = SDL_GetPerformanceCounter();
  if (!shot_hide_hud_) Overlay::draw(this, glship);
  perf_osd_pc_ += SDL_GetPerformanceCounter() - pc0;
  Typer::scaled_window_width = saved_sw;
}

void GLGame::draw_perspective(GLShip *glship) const {
  /* Draw the world */
  Point position = (glship == NULL) ? Point(0,0) : glship->ship->position;
  float direction = (glship == NULL || !glship->rotate_view()) ? 0.0f : glship->camera_facing();

  // Starfields are Mesh-based (GPU-resident), drawn directly in each tile.

  // Compute cull radius from actual viewport dimensions.
  // Camera is at z=1000; gluPerspective FOV is vertical.
  float fov_deg = glship ? glship->view_angle() : 85.0f;
  float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
  float aspect = (window.x() / (float)num_x_viewports()) /
                 (window.y() / (float)num_y_viewports());
  float half_w = half_h * aspect;
  float cull_r2 = (half_w * half_w + half_h * half_h) * 1.1f; // 10% margin for edge objects

  // Read the perspective*lookat VP set by draw_world; tile transforms are layered on top.
  float base_pv[16]; gles2_get_mvp(base_pv);

  // Draw the world tessellated 3x3, culling tiles that are entirely off-screen.
  Uint64 pc0 = SDL_GetPerformanceCounter();
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      // Nearest distance from camera to tile rectangle (starfield tiles are
      // centered on world origin, not on the player).
      float smin_x = world.x()*x - position.x();
      float smax_x = smin_x + world.x();
      float smin_y = world.y()*y - position.y();
      float smax_y = smin_y + world.y();
      float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
      float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
      if (snx*snx + sny*sny > cull_r2) continue;

      float tile_vp[16];
      mat4_rotate_z(tile_vp, base_pv, direction);
      mat4_translate(tile_vp, tile_vp, world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);
      gles2_set_vp(tile_vp);
      starfield->draw_rear(position);
    }
  }
  perf_stars_pc_ += SDL_GetPerformanceCounter() - pc0;
  // --- Invisible asteroid lensing: black asteroid polygon + shifted rear stars ---
  // Lensing is expensive per asteroid (the mask mesh re-uploads and
  // draw_stars_near's cache rebuild scans every star), so unlike the batched
  // object pass each asteroid is culled to the screen individually — only
  // the handful actually in view pay. lens_on_screen then gates the warp
  // capture below: off-screen invisible asteroids must not cost a
  // full-viewport texture copy either.
  float cull_r = sqrtf(cull_r2);
  bool lens_on_screen = false;
  Uint32 lens_t0 = SDL_GetTicks();
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      float smin_x = world.x()*x - position.x();
      float smax_x = smin_x + world.x();
      float smin_y = world.y()*y - position.y();
      float smax_y = smin_y + world.y();
      float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
      float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
      if (snx*snx + sny*sny > cull_r2) continue;

      float tile_vp[16];
      mat4_rotate_z(tile_vp, base_pv, direction);
      mat4_translate(tile_vp, tile_vp, world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);
      gles2_set_vp(tile_vp);

      for(list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
        Asteroid const *a = *it;
        if (!a->invisible || !a->alive) continue;

        float ax = a->position.x() + a->net_pose_err.x();
        float ay = a->position.y() + a->net_pose_err.y();
        float rel_x = ax + world.x()*x - position.x();
        float rel_y = ay + world.y()*y - position.y();
        float reach = cull_r + a->radius;
        if (rel_x*rel_x + rel_y*rel_y > reach*reach) continue;
        lens_on_screen = true;
        AsteroidDrawer::draw_invisible_mask(a, ax, ay);
        starfield->draw_stars_near(ax, ay, a->radius);
      }
    }
  }
  Uint32 lens_frame_ms = SDL_GetTicks() - lens_t0;

  // Game objects: drawn directly each tile (no display list) so draw_batch
  // can emit all asteroids in two draw calls per tile instead of one per asteroid.
  pc0 = SDL_GetPerformanceCounter();
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      // Nearest distance from camera to tile rect (objects span [0,world) per tile)
      float tmin_x = world.x()*x - position.x();
      float tmax_x = tmin_x + world.x();
      float tmin_y = world.y()*y - position.y();
      float tmax_y = tmin_y + world.y();
      float tnx = (tmin_x > 0) ? tmin_x : (tmax_x < 0) ? -tmax_x : 0;
      float tny = (tmin_y > 0) ? tmin_y : (tmax_y < 0) ? -tmax_y : 0;
      if (tnx*tnx + tny*tny > cull_r2) continue;

      float tile_vp[16];
      mat4_rotate_z(tile_vp, base_pv, direction);
      mat4_translate(tile_vp, tile_vp, world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);
      gles2_set_vp(tile_vp);
      // Camera centre in this tile's object space, for per-asteroid culling.
      draw_objects(direction, false,
                   position.x() - world.x()*x, position.y() - world.y()*y,
                   cull_r);
    }
  }
  perf_objs_pc_ += SDL_GetPerformanceCounter() - pc0;
  pc0 = SDL_GetPerformanceCounter();
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      float smin_x = world.x()*x - position.x();
      float smax_x = smin_x + world.x();
      float smin_y = world.y()*y - position.y();
      float smax_y = smin_y + world.y();
      float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
      float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
      if (snx*snx + sny*sny > cull_r2) continue;

      float tile_vp[16];
      mat4_rotate_z(tile_vp, base_pv, direction);
      mat4_translate(tile_vp, tile_vp, world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);
      gles2_set_vp(tile_vp);
      starfield->draw_front(position);
    }
  }
  perf_stars_pc_ += SDL_GetPerformanceCounter() - pc0;

  // --- Front star lensing (same void + shift, applied after front stars) ---
  lens_t0 = SDL_GetTicks();
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      float smin_x = world.x()*x - position.x();
      float smax_x = smin_x + world.x();
      float smin_y = world.y()*y - position.y();
      float smax_y = smin_y + world.y();
      float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
      float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
      if (snx*snx + sny*sny > cull_r2) continue;

      float tile_vp[16];
      mat4_rotate_z(tile_vp, base_pv, direction);
      mat4_translate(tile_vp, tile_vp, world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);
      gles2_set_vp(tile_vp);

      for(list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
        Asteroid const *a = *it;
        if (!a->invisible || !a->alive) continue;

        float ax = a->position.x() + a->net_pose_err.x();
        float ay = a->position.y() + a->net_pose_err.y();
        float rel_x = ax + world.x()*x - position.x();
        float rel_y = ay + world.y()*y - position.y();
        float reach = cull_r + a->radius;
        if (rel_x*rel_x + rel_y*rel_y > reach*reach) continue;
        // No black mask here — draw_batch already renders the invisible asteroid
        // fill behind visible asteroids. Drawing it again after game objects would
        // overdraw visible asteroids on top of invisible ones.
        starfield->draw_front_stars_near(ax, ay, a->radius);
      }
    }
  }

  // --- Warp pass: distort the contents of each invisible asteroid ---
  // Gated on an invisible asteroid actually being ON SCREEN (set by the
  // rear lens loop) — merely existing somewhere in the world must not cost
  // the full-viewport texture copy every frame.
  if (lens_on_screen) {
    // Snapshot the current viewport (stars + game objects) into the warp texture.
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    warp_pass_->capture(vp[0], vp[1], vp[2], vp[3]);

    // Re-run the tile loop with the warp shader to overwrite each invisible
    // asteroid's black fill with the gravitational-lens distortion.
    for (int x = -1; x <= 1; x++) {
      for (int y = -1; y <= 1; y++) {
        float smin_x = world.x()*x - position.x();
        float smax_x = smin_x + world.x();
        float smin_y = world.y()*y - position.y();
        float smax_y = smin_y + world.y();
        float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
        float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
        if (snx*snx + sny*sny > cull_r2) continue;

        float tile_vp[16];
        mat4_rotate_z(tile_vp, base_pv, direction);
        mat4_translate(tile_vp, tile_vp, world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);
        gles2_set_vp(tile_vp);

        for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
          Asteroid const *a = *it;
          if (!a->invisible || !a->alive) continue;
          float ax = a->position.x() + a->net_pose_err.x();
          float ay = a->position.y() + a->net_pose_err.y();
          float rel_x = ax + world.x()*x - position.x();
          float rel_y = ay + world.y()*y - position.y();
          float reach = cull_r + a->radius;
          if (rel_x*rel_x + rel_y*rel_y > reach*reach) continue;
          warp_pass_->draw(a, ax, ay, vp[0], vp[1], vp[2], vp[3]);
        }
      }
    }
    gles2_set_vp(base_pv);
  }
  lens_frame_ms += SDL_GetTicks() - lens_t0;
  perf_lens_ms_ += lens_frame_ms;
  if (lens_frame_ms > perf_lens_max_) perf_lens_max_ = lens_frame_ms;
}

void GLGame::draw_map() const {
  // No minimap on touch (and on forced-touch screenshot renders, which
  // must match the devices). Gated on is_touch_mode(), NOT the OSD: web
  // draws its on-screen controls from the page (web/main.ts), so
  // touch_osd_enabled() is false there and a phone browser kept the
  // minimap while every other layout had gone touch.
  if (is_touch_mode()) return;
  // Solo: corner map. 2-player strip split: small map on the divider. 2x2
  // grid: at 3 players the free bottom-right cell hosts a bigger map (it
  // owns the whole quadrant); at 4 a small one sits on the centre crosshair.
  bool grid = split_screen() && num_x_viewports() == 2 && num_y_viewports() == 2;
  bool free_cell_map = grid && players->size() == 3;
  float minimap_size = !split_screen() ? window.y()/4
                     : free_cell_map   ? window.y()/3
                                       : window.y()/6;

  if(split_screen()) {
    /* DRAW THE VIEWPORT DIVIDERS */
    float center_ortho[16];
    mat4_ortho(center_ortho, -(float)window.x(), (float)window.x(),
               -(float)window.y(), (float)window.y(), -1.0f, 1.0f);
    gles2_set_vp(center_ortho);
    glViewport(0, 0, window.x(), window.y());
    {
      static MeshBuilder mb;
      static Mesh mesh;
      mb.clear();
      mb.begin(GL_LINES);
      mb.color(1.0f, 1.0f, 1.0f, 0.5f);
      if (grid) {
        // Cross through the centre; each arm splits around the minimap only
        // where the map actually sits on it (4P centre — at 3P the map is
        // in the free cell, off both lines).
        float gap = free_cell_map ? 0.0f : minimap_size;
        mb.vertex(0, -window.y()); mb.vertex(0, -gap);
        mb.vertex(0, gap); mb.vertex(0, window.y());
        mb.vertex(-window.x(), 0); mb.vertex(-gap, 0);
        mb.vertex(gap, 0); mb.vertex(window.x(), 0);
      } else if(window.x() < window.y()) {
        mb.vertex(-window.x(), 0); mb.vertex(-minimap_size, 0);
        mb.vertex(minimap_size, 0); mb.vertex(window.x(), 0);
      } else {
        mb.vertex(0, -window.y()); mb.vertex(0, -minimap_size);
        mb.vertex(0, minimap_size); mb.vertex(0, window.y());
      }
      mb.end();
      mesh.upload(mb, GL_DYNAMIC_DRAW);
      mesh.draw();
    }
  }

  /* MINIMAP */
  {
    float minimap_ortho[16];
    mat4_ortho(minimap_ortho, 0.0f, (float)world.x(), 0.0f, (float)world.y(), -1.0f, 1.0f);
    gles2_set_vp(minimap_ortho);
  }
  if (!split_screen()) {
    // Shift the minimap right of the virtual joystick so they don't
    // overlap (wherever the OSD draws — see touch_osd_enabled()).
    int map_x = touch_osd_enabled()
        ? (int)(g_touch_controls.joy_hint_cx + g_touch_controls.joy_radius +
                Overlay::CORNER_INSET)
        : (int)Overlay::CORNER_INSET;
    // The minimap is positioned with a raw pixel viewport, so the safe-area
    // margin must be added here in pixels (the HUD ortho trick can't reach it).
    int safe_px_x = (int)(window.x() * (1.0f - Overlay::SAFE_AREA_SCALE) / 2.0f);
    int safe_px_y = (int)(window.y() * (1.0f - Overlay::SAFE_AREA_SCALE) / 2.0f);
    glViewport(map_x + safe_px_x, (int)Overlay::CORNER_INSET + safe_px_y, minimap_size, minimap_size);
  } else if (free_cell_map) {
    // Centre of the free bottom-right cell (GL origin bottom-left).
    glViewport(window.x()*3/4 - minimap_size/2, window.y()/4 - minimap_size/2,
               minimap_size, minimap_size);
  } else {
    glViewport(window.x()/2 - minimap_size/2, window.y()/2 - minimap_size/2, minimap_size, minimap_size);
  }

  /* BLACK BOX OVER MINIMAP */
  {
    static MeshBuilder mb;
    static Mesh mesh;
    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(0.0f, 0.0f, 0.0f, 0.8f);
    float wx = (float)world.x(), wy = (float)world.y();
    mb.vertex(0, 0);    mb.vertex(wx, 0);  mb.vertex(wx, wy);
    mb.vertex(0, 0);    mb.vertex(wx, wy); mb.vertex(0, wy);
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }

  // Single draw pass for minimap; wrapping tiles are negligible at minimap scale.
  draw_objects(0.0f, true);

  /* LINE AROUND MINIMAP */
  {
    static MeshBuilder mb;
    static Mesh mesh;
    mb.clear();
    mb.begin(GL_LINE_LOOP);
    mb.color(0.5f, 0.5f, 0.5f, 1.0f);
    float wx = (float)world.x(), wy = (float)world.y();
    mb.vertex(0, 0); mb.vertex(wx, 0); mb.vertex(wx, wy); mb.vertex(0, wy);
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }
}

// Pad-button → logical-key vocabulary for the leaderboard prompt (dpad
// moves, A/Start confirm, B/Back = Esc), shared by the healthy game-over
// block and the connection-lost card path below so the two prompts can't
// drift apart. 0 = not a prompt key.
static char board_pad_key(const SDL_Event &event) {
  if (event.type != SDL_CONTROLLERBUTTONDOWN) return 0;
  switch (event.cbutton.button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return 'w';
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return 's';
    case SDL_CONTROLLER_BUTTON_A:
    case SDL_CONTROLLER_BUTTON_START: return '\r';
    case SDL_CONTROLLER_BUTTON_B:
    case SDL_CONTROLLER_BUTTON_BACK: return 27;
    default: return 0;
  }
}

void GLGame::controller(SDL_Event event) {
  if (net_card_owns_input()) {
    // Same one-frame guard as keyboard_up: a committed auto-rejoin
    // hand-off must not be overwritten (the pending lobby would leak).
    if (net_handed_to_lobby_) return;
    // The leaderboard prompt outranks the card — the keyboard and touch
    // twins' ordering. Everything else is swallowed while it owns the
    // game-over card, like the keyboard twin, so no button exits through
    // the prompt. RT confirms it exactly as on the healthy game-over card
    // (board_nav carries the 3 s grace and the YES default itself).
    if (board_prompt_active()) {
      char bk = board_pad_key(event);
      if (event.type == SDL_CONTROLLERAXISMOTION &&
          event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
          event.caxis.value > 8000)
        bk = '\r';
      if (bk) board_nav(bk);
      return;
    }
    // A/Start confirm the card's row, B/Back leave — buttons ONLY: a
    // button DOWN is a fresh press by construction, the pad's equivalent
    // of net_card_pressed_. The trigger is deliberately NOT a confirm
    // here, unlike the healthy game-over card: gameplay routes trigger
    // events to the ships, never the nav translator, so the translator's
    // RT edge state is stale-false during play, and a trigger held to
    // FIRE through the disconnect (or jittering on its release ramp)
    // would register as a fresh confirm and throw the rejoinable session
    // away — the held-fire race, pad edition.
    if (event.type == SDL_CONTROLLERBUTTONDOWN &&
        is_exit_key(nav_key_from_controller(event))) {
      if (all_players_out() && game_over_grace_active()) return;
      request_state_change(new Menu());
    }
    return;
  }
  // Replay playback: Start pauses, B (or any button once the recording's
  // game over has sat 3 s) exits; ghosts take no pad input.
  if (net_mode_ == NetReplay) {
    // Paused, the pause menu answers the pad exactly as it does offline
    // (dpad/stick move, A confirms) — the keyboard twin above.
    if (pause_menu_active()) {
      unsigned char nav = nav_key_from_controller(event);
      if (MenuSelect::is_up(nav) || MenuSelect::is_down(nav) ||
          MenuSelect::is_confirm(nav)) {
        pause_nav(nav);
        return;
      }
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) toggle_pause();
      else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
               (replay_exit_offered() &&
                is_exit_key(nav_key_from_controller(event))))
        request_state_change(new Menu());
    }
    return;
  }
  // Seat roster: a pad ALREADY driving a seat navigates it; a pad driving
  // nothing claims the highlighted seat by pressing any button, which is
  // how a new player joins here instead of through the START ladder below.
  if (roster_open()) {
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
      if (!is_player_controller(event.cbutton.which)) {
        roster_claim_pad(event.cbutton.which);
        return;
      }
      roster_nav(nav_key_from_controller(event));
      return;
    }
    if (event.type == SDL_CONTROLLERAXISMOTION &&
        is_player_controller(event.caxis.which)) {
      unsigned char nav = nav_key_from_controller(event);
      if (nav) roster_nav(nav);
    }
    return;
  }

  // Paused with the menu up: dpad and left stick move the highlight, A
  // confirms — but only from a pad that is already playing, so an unknown
  // pad's A still joins player 2 through the ladder below. START (toggle
  // pause) and BACK (exit) fall through untouched, so the pad shortcuts
  // are exactly as they were.
  if (pause_menu_active()) {
    if (event.type == SDL_CONTROLLERBUTTONDOWN &&
        is_player_controller(event.cbutton.which)) {
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
        pause_nav('w');
        return;
      }
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
        pause_nav('s');
        return;
      }
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
        pause_nav('\r');
        return;
      }
      if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
        // B backs out one level — the same thing it means on every other
        // screen; here that closes the pause menu (resume). It used to do
        // nothing while paused (field, 2026-08-08). BACK below stays the
        // quit-to-menu shortcut, START still resumes directly.
        toggle_pause();
        return;
      }
    } else if (event.type == SDL_CONTROLLERAXISMOTION &&
               is_player_controller(event.caxis.which)) {
      // Stick nav through the shared arm/release hysteresis, so the pause
      // menu feels like every other screen. Only w/s are taken — the
      // translator also maps the right trigger to confirm, and in-game
      // that trigger is fire.
      unsigned char nav = nav_key_from_controller(event);
      if (MenuSelect::is_up(nav) || MenuSelect::is_down(nav)) {
        pause_nav(nav);
        return;
      }
    }
  }

  if(event.cbutton.type == SDL_CONTROLLERBUTTONDOWN) {
    // Leaderboard prompt on the GAME OVER card: while it owns the card,
    // dpad moves the YES/NO highlight and A/Start confirm, B backs out —
    // ahead of every exit shortcut below so a pad can't leave through the
    // prompt. board_nav is a no-op returning false once the prompt is done.
    if (board_prompt_active()) {
      char bk = board_pad_key(event);
      if (bk && board_nav(bk)) return;
    }
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
      bool known_player = false;
      for(auto* glship : *players) {
        if(glship->wasMyController(event.cbutton.which)) {
          known_player = true;
          break;
        }
      }
      if(known_player) {
        bool all_game_over = !players->empty();
        for (auto* glship : *players) {
          if (glship->ship->is_alive() || glship->ship->lives > 0) {
            all_game_over = false;
            break;
          }
        }
        if (all_game_over) {
          if (!game_over_grace_active()) {
            for (auto* glship : *players)
              save_high_score(glship->ship->score);
            request_state_change(new Menu());
          }
        } else {
          toggle_pause();
        }
      } else if((int)players->size() < LOCAL_PLAYER_CAP && net_mode_ == NetOff) {
        SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event.cbutton.which);
        if(ctrl) add_local_player(ctrl, /*with_keys=*/false);
      }
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_X ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
      bool known_player = false;
      for(auto* glship : *players) {
        if(glship->wasMyController(event.cbutton.which)) {
          known_player = true;
          break;
        }
      }
      if(known_player) {
        bool all_game_over = !players->empty();
        for (auto* glship : *players) {
          if (glship->ship->is_alive() || glship->ship->lives > 0) {
            all_game_over = false;
            break;
          }
        }
        if (all_game_over) {
          // A and B are the card's confirm and back; X and Y are neither,
          // so they no longer leave (they still join player 2 below on a
          // pad that isn't playing yet).
          if (is_exit_key(nav_key_from_controller(event)) &&
              !game_over_grace_active()) {
            for (auto* glship : *players)
              save_high_score(glship->ship->score);
            request_state_change(new Menu());
          }
          return;
        }
      } else if((int)players->size() < LOCAL_PLAYER_CAP && net_mode_ == NetOff) {
        SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event.cbutton.which);
        if(ctrl) add_local_player(ctrl, /*with_keys=*/false);
      }
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
      if(running && pad_may_command(event.cbutton.which)) toggle_pause();
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK &&
               pad_may_command(event.cbutton.which)) {
      // Exactly what the keyboard menu key (and back_pressed) does — save
      // first, then hand over. This used to bank only the HIGH SCORE: a
      // BACK during live play threw away all progress since the last
      // auto-save, and banked an unfinished run's score, which the
      // keyboard path never did (field, 2026-08-08). And like that key,
      // online BACK opens/closes the pause menu instead of tearing down
      // the room — the exit is the menu's EXIT TO MENU row.
      if ((net_mode_ == NetHost || net_mode_ == NetClient) &&
          !all_players_out()) {
        toggle_pause();
      } else {
        save_progress();
        request_state_change(new Menu());
      }
    }
  }

  if(event.type == SDL_CONTROLLERAXISMOTION &&
     event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
     event.caxis.value > 8000 && pad_may_command(event.caxis.which)) {
    bool all_game_over = !players->empty();
    for (auto* glship : *players) {
      if (glship->ship->is_alive() || glship->ship->lives > 0) {
        all_game_over = false;
        break;
      }
    }
    if (all_game_over) {
      // RT is the card's confirm: while the leaderboard prompt is up it
      // answers the highlighted YES/NO instead of leaving.
      if (board_nav('\r')) return;
      if (game_over_grace_active())
        return;
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      request_state_change(new Menu());
      return;
    }
  }

  if(!running)
    return;

  if(event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
    std::list<GLShip*>::iterator object;
    for(object = players->begin(); object != players->end(); object++) {
      (*object)->controller_input(event);
    }
  }
  if(event.type == SDL_CONTROLLERAXISMOTION) {
    std::list<GLShip*>::iterator object;
    for(object = players->begin(); object != players->end(); object++) {
      (*object)->controller_axis_input(event);
    }
  }
  if(event.type == SDL_CONTROLLERTOUCHPADDOWN ||
     event.type == SDL_CONTROLLERTOUCHPADMOTION ||
     event.type == SDL_CONTROLLERTOUCHPADUP) {
    std::list<GLShip*>::iterator object;
    for(object = players->begin(); object != players->end(); object++) {
      (*object)->controller_touchpad_input(event);
    }
  }
}

// The friendly-fire toggle band hugs the viewport bottom, kept to the
// centre third: the OSD shoot/mine hit circles reach into the bottom
// strip on the right, and the floating joystick can release on the left.
TapBand GLGame::ff_toggle_band() const {
  float vhb = -Typer::scaled_window_height / num_y_viewports();
  return TapBand(0.5f, vhb + 55, 8, 45.0f, false, /*to_bottom=*/true,
                 0.38f, 0.62f);
}

// Landscape keeps the shared lobby band: -420 IS the bottom strip when
// the virtual half-height is 600. Portrait stretches the virtual height
// (600/aspect, ~1300 on a tall phone), which left that anchor a third of
// the way up the screen — the label printed right at the joystick's top
// edge and the to_bottom tap region swallowed the entire touch-controls
// area, so a stray tap near the (inert but thumb-parked) controls on the
// pause screen quit to the menu — and online that deliberate teardown
// closes the room (field: Android portrait, 2026-07-25). Re-anchor it
// just above the bottom text rows (badge rows at vhb+130 and vhb+168,
// SPECTATING at vhb+130, friendly-fire at vhb+55) instead: label and tap
// region sit between that text and the controls, touching neither — and
// whenever this band is actually shown the badge rows hoist to
// vhb+255/vhb+293 (Overlay::net_badges via exit_band_showing), clear
// above it in both orientations.
TapBand GLGame::exit_band() const {
  if (Typer::scaled_window_height <= Typer::original_window_height)
    return TapBand::return_to_menu;
  float vhb = -Typer::scaled_window_height / num_y_viewports();
  return TapBand(0.5f, vhb + 215, 13, 35.0f);
}

bool GLGame::exit_band_showing() const {
  if (!is_touch_mode()) return false;
  if (net_mode_ == NetReplay) return false;  // replay chrome owns its bands
  bool all_over = !players->empty();
  for (auto *gs : *players)
    if (gs->ship->is_alive() || gs->ship->lives > 0) { all_over = false; break; }
  const GLShip *local = local_player();
  bool local_over = net_active() && local && !local->ship->is_alive() &&
                    local->ship->lives <= 0;
  // B5: only an EMPTY roster shows the band mid-play — one lost seat
  // under play-on is not an exit moment for a touch host.
  return all_over || !running || local_over || net_all_peers_lost();
}

void GLGame::touch_tap(float nx, float ny) {
  if (!is_touch_mode()) return;
  // Leaderboard prompt on the GAME OVER card: a tap on the EXIT TO MENU
  // band still LEAVES (it is drawn under the prompt), and only taps
  // elsewhere answer YES (left half) / NO (right half) — the New-game
  // confirm's grammar. The arm delay (board_nav) rejects a tap already in
  // flight when the prompt appeared.
  if (board_phase_ == BoardPrompt) {
    if (exit_band().contains(nx, ny)) {
      // Decline and leave, the same as answering NO then exiting.
      if (current_time - board_prompt_shown_ >= BOARD_PROMPT_ARM_MS) {
        board_yes_ = false;
        board_nav('\r');
      }
      // fall through to the exit band handling below (which leaves)
    } else {
      // Left half = YES, right half = NO — the New-game confirm's grammar
      // (nx is normalized 0..1, so the half-split is 0.5f: this read
      // `nx < 0.0f`, which is never true, and EVERY tap answered NO — the
      // prompt just vanished with no upload; field, client on a touch
      // device, 2026-08-02).
      board_yes_ = nx < 0.5f;
      board_nav('\r');
      return;
    }
  }
  if (board_phase_ == BoardUploading && !exit_band().contains(nx, ny))
    return;
  // The bottom strip is the EXIT TO MENU band the overlay labels (the
  // shared TapBand). It exits to the menu from every state that has no
  // other touch exit: GAME OVER, the pause screen, and — online — a
  // local ship that's fully out while the peer plays on.
  // Replay playback controls (labels drawn by Overlay::replay_hud — the
  // TapBand rule: one definition for text and hit-test).
  if (net_mode_ == NetReplay) {
    // The SAME lift Overlay::replay_hud draws these with — the TapBand
    // invariant is only held if both sides transform alike.
    float lift = TapBand::bottom_lift();
    if (TapBand::replay_pause.lifted(lift).contains(nx, ny)) {
      toggle_pause();
      return;
    }
    if (TapBand::replay_slower.lifted(lift).contains(nx, ny)) {
      if (replay_speed_ > 0.26f) replay_speed_ *= 0.5f;
      return;
    }
    if (TapBand::replay_faster.lifted(lift).contains(nx, ny)) {
      if (replay_speed_ < 4.0f) replay_speed_ *= 2.0f;
      return;
    }
    // The exit band always exits (nothing in a replay needs protecting
    // from an accidental tap).
    if (TapBand::return_to_menu.lifted(lift).contains(nx, ny))
      request_state_change(new Menu());
    return;
  }
  if (!exit_band().contains(nx, ny)) {
    // Portrait moved the exit strip off the bottom text, so the
    // friendly-fire toggle no longer nests inside it — hit-test it
    // independently (mid-play, host, 2P, same gates as the tail below).
    if (running && players->size() >= 2 && net_mode_ != NetClient &&
        ff_toggle_band().contains(nx, ny))
      host_toggle_friendly_fire();
    return;
  }
  if (!exit_band_showing()) {
    // Band not on screen: the tap is just a tap near the bottom. Mid-play,
    // the "friendly fire on/off" HUD text (two-player only) is a toggle
    // region — host only, mirroring the G key.
    if (players->size() >= 2 && net_mode_ != NetClient &&
        ff_toggle_band().contains(nx, ny))
      host_toggle_friendly_fire();
    return;
  }
  // The band is up — it has to actually leave, or it is a lie (that ONE
  // rule, exit_band_showing, is shared with the overlay's draw site; a
  // lost link counts, so touch has one way out of every end-state instead
  // of the card's old "tap fire"). The branches keep their own guards.
  bool all_game_over = !players->empty();
  for (auto *glship : *players) {
    if (glship->ship->is_alive() || glship->ship->lives > 0) {
      all_game_over = false;
      break;
    }
  }
  if (all_game_over) {
    // The 3 s grace mirrors the key/controller exits so a frantic
    // last-second tap can't skip past the game over screen.
    if (game_over_grace_active()) return;
    for (auto *glship : *players)
      save_high_score(glship->ship->score);
    request_state_change(new Menu());
    return;
  }
  // A paused game, a fully-out local ship spectating on, or a lost link.
  // Same one-frame guard as keyboard_up/controller: a committed
  // auto-rejoin hand-off must not be overwritten by the exit band.
  if (net_handed_to_lobby_) return;
  save_progress();
  request_state_change(new Menu());
}

// How hard a lance pulse hits the gen-20 station's hull, in bullet
// equivalents (bullets and missiles do 1 each).
static const int LANCE_STATION_DAMAGE = 3;

// A client lance pulse or shock bolt ends where something blocked it, and
// what happens to that blocker is HOST authority: the client deliberately
// does not claim an asteroid its local rules say SURVIVES the hit
// (claiming would kill it — the claim handler forces claims through), it
// just stops its weapon there. But nothing host-side ever made the call —
// asteroid kills arrive as MSG_HIT claims and the polyline resolutions
// only covered ships/hulls — so a ready-to-teleport asteroid never evaded
// a client lance (field, 2026-08-02), and a client shock could not chip a
// tough rock. Find a SURVIVOR-type asteroid at the polyline's endpoint
// and kill() it — kill() then does exactly what the offline hit does per
// type: teleporting evades (debris + teleport_pending), tough chips one
// health (crack feedback), a phased ghost / invincible rock just plays
// its feedback. The guard list is strict so a plain killable rock that
// happens to sit at a faded bolt's tip can never be destroyed by this —
// kills only ever arrive as claims.
void GLGame::net_resolve_polyline_block(const std::vector<Point> &pts) {
  if (pts.size() < 2) return;
  const Point &end = pts.back();
  for (Asteroid *ast : *objects) {
    if (!ast->is_alive()) continue;
    bool survivor = ast->invincible ||
                    (ast->teleporting && !ast->teleport_vulnerable) ||
                    (ast->phasing && ast->phased) ||
                    (ast->tough && ast->health > 1);
    if (!survivor) continue;
    // The endpoint sits ON the blocking surface (the march's segment_hit
    // entry point / the bolt's stop() collision point), so centre distance
    // ~= radius; small slack for the client/host position skew at 10 Hz.
    // Wrapped-world translation first, like every other cross-copy test.
    Point centre = ast->position.closest_to(end);
    float dx = centre.x() - end.x(), dy = centre.y() - end.y();
    float reach = ast->radius + 6.0f;
    if (dx * dx + dy * dy <= reach * reach) {
      ast->kill();  // evade / chip / feedback — never a death (see guard)
      break;
    }
  }
}

void GLGame::resolve_lance_ship_hits(Ship *firer, const std::vector<Point> &pts) {
  if (pts.size() < 2) return;

  // The first mirror bounce: a killed asteroid is passed through
  // collinearly, a reflection bends the path — so the first direction
  // change between consecutive segments marks where the pulse can start
  // hitting its own firer. (A near-grazing reflection under the threshold
  // barely deviates and can't come back at the firer anyway.)
  size_t first_refl_seg = pts.size();
  for (size_t i = 1; i + 1 < pts.size(); i++) {
    float ax = pts[i].x() - pts[i - 1].x(), ay = pts[i].y() - pts[i - 1].y();
    float bx = pts[i + 1].x() - pts[i].x(), by = pts[i + 1].y() - pts[i].y();
    float am = sqrtf(ax * ax + ay * ay), bm = sqrtf(bx * bx + by * by);
    if (am <= 1e-3f || bm <= 1e-3f) continue;
    if ((ax * bx + ay * by) / (am * bm) < 0.9995f) { first_refl_seg = i; break; }
  }

  // First segment (from from_seg on) that enters the target's circle;
  // fills the hull-entry point (the impact position for debris).
  auto first_hit = [&](const Ship *target, size_t from_seg, Point *where) {
    for (size_t s = from_seg; s + 1 < pts.size(); s++)
      if (lance_seg_circle_entry(pts[s], pts[s + 1], target->position,
                                 target->radius, where))
        return true;
    return false;
  };

  Point where;

  // Self: reflected segments only (the outgoing beam leaves the firer's
  // own muzzle). kill_stop() gates shields/invincibility like every other
  // ship-vs-ship weapon; no score for shooting yourself.
  if (first_refl_seg + 1 < pts.size() && firer->is_alive() &&
      first_hit(firer, first_refl_seg, &where)) {
    firer->kill_stop();
  }

  // Partner: any segment, friendly fire only.
  if (friendly_fire) {
    for (auto *p : *players) {
      Ship *t = p->ship;
      if (t == firer || !t->is_alive()) continue;
      if (first_hit(t, 0, &where) && t->kill_stop())
        firer->credit_ship_kill(t);
    }
  }

  // Enemies: always.
  for (auto *e : *enemies) {
    Ship *t = e->ship;
    if (!t->is_alive()) continue;
    if (first_hit(t, 0, &where) && t->kill_stop())
      firer->credit_ship_kill(t);
  }

  // Mini-station: dies to any hit, flat bounty — mirror of the bullet
  // path, including the attenuated boom + net relay.
  if (mini_station != NULL && mini_station->is_alive() &&
      first_hit(mini_station, 0, &where)) {
    firer->explode(where, mini_station->velocity);
    firer->score += GLMiniStation::REWARD;
    mini_station->destroy();
    if (firer->is_local_player) {
      Achievements::unlock("mini_station_kill");
      Achievements::progress("score_3m", firer->score / 30000);
    } else if (NetPeer *pr = net_peer_for_ship(firer)) {
      net_send_event_to(*pr, Net::EV_ACHIEVEMENT, Net::ACH_MINI_STATION_KILL);
    }
    play_priority_chunk(station_explode_sound,
                        world_volume(mini_station->position));
    net_send_event(Net::EV_STATION_BOOM,
                   Net::pack_pos(mini_station->position.x(),
                                 mini_station->position.y(),
                                 world.x(), world.y()));
  }

  // Station: the hull takes a chunk of damage (it survives the pulse
  // visually — a ring is plausible to lance through). Same thud cue and
  // net relay as a hull bullet deflection.
  if (station != NULL && station->is_alive() &&
      first_hit(station, 0, &where)) {
    // Debris splash at the initial hull contact, like the bullet path's
    // deflection spray.
    firer->explode(where, station->velocity);
    for (int i = 0; i < LANCE_STATION_DAMAGE && station->is_alive(); i++)
      station->hit();
    if (!station->is_alive()) {
      if (firer->is_local_player) Achievements::unlock("station_destroyed");
      else if (NetPeer *pr = net_peer_for_ship(firer))
        net_send_event_to(*pr, Net::EV_ACHIEVEMENT,
                          Net::ACH_STATION_DESTROYED);
    }
    WorldSound::play(Asteroid::thud_sound, where);
    // A client firer already played its own splash + thud at contact
    // (the PROTO 20 pass in tick_net_client) — relaying to THEM would
    // double it, but the other peers still need the cue. Local firer:
    // the plain broadcast (with its replay tee). Remote firer: targeted
    // sends skipping the firer, no tee — the host's replay keeps its
    // pre-B7 stream (the firing client's file tees at its own site).
    NetPeer *thud_firer = net_peer_for_ship(firer);
    if (!thud_firer) {
      net_send_event(Net::EV_ROID_THUD,
                     Net::pack_pos(where.x(), where.y(), world.x(), world.y()));
    } else {
      for (NetPeer *op : net_peers_)
        if (op != thud_firer)
          net_send_event_to(*op, Net::EV_ROID_THUD,
                            Net::pack_pos(where.x(), where.y(), world.x(),
                                          world.y()),
                            /*tee=*/false);
    }
  }
}

// Put a fully-out partner back on their last life; the ordinary respawn
// machinery does the rest (lives=1 restarts the parked countdown; the
// respawn charges the life back to 0 = the standard alive-on-last-life
// state). Online this runs host-side; lives/alive replicate, the fallen
// client's spectate ends by itself and the alive-transition respawns the
// wreck. No fallen partner (they beat the pickup back some other way) is
// a quiet no-op. except = the collector (never revives itself).
void GLGame::revive_fallen_partner(Ship *except) {
  // Longest-dead first. At 2P there was only ever one partner to revive, so
  // "the first fallen in the list" was unambiguous; with 3-4 seats it meant
  // the LOWEST SEAT always won however recently they fell, and a player who
  // had been waiting since the last generation could watch the seat below
  // them jump the queue. Ship::out_order() stamps a monotonic counter the
  // moment a ship runs out of lives, so the smallest stamp is whoever has
  // been out longest. An UNSTAMPED ship (0) is the freshest kind there is,
  // not the oldest: step() stamps at the fully-out transition, and the only
  // way to reach here before that is to have died in THIS tick's collision
  // pass, which runs after the step loops and before pickup collection. So
  // 0 sorts newest — treating it as oldest let a player who died a
  // millisecond ago jump the whole queue, the exact inversion this fixes.
  // (A restored save needs no special case: its out players are stamped on
  // the first step, well before any pickup can be collected, and they take
  // list order among themselves — the old behaviour, for those.)
  Ship *best = NULL;
  unsigned best_rank = 0;
  for (auto *gs : *players) {
    Ship *fallen = gs->ship;
    if (fallen == except) continue;
    if (fallen->is_alive() || fallen->lives > 0) continue;  // not fully out
    unsigned rank = fallen->out_order();
    if (rank == 0) rank = ~0u;  // out this very tick: newest
    if (best == NULL || rank < best_rank) {
      best = fallen;
      best_rank = rank;
    }
  }
  if (best == NULL) return;
  unsigned order = best->out_order();
  best->revive_one_life();
  // Name the seat: with more than one player down, "a partner" no longer
  // identifies anyone, and this is the only record of which way the
  // longest-dead pick went.
  NET_LOG("revive - player %d respawning (out order %u)\n",
          (int)best->net_seat, order);
}

// Time-slow pickup collected: slow the WORLD's wall-clock rate for
// kTimeSlowWallMs while the collector keeps wall-normal turning — an aiming
// window (see the members in glgame.h for the full story). The countdown
// runs in sim ms, so the wall duration divides by the factor here. A second
// clock collected mid-effect refills the window; if the other player grabs
// it, the compensation moves with it. Runs offline and host-side alike
// (collection lives in the authoritative sim); the client/replay side only
// ever adopts the replicated state in net_apply_state.
void GLGame::start_time_slow(Ship *collector) {
  if (net_mode_ == NetClient || net_mode_ == NetReplay) return;
  if (time_slow_ship_ != NULL && time_slow_ship_ != collector)
    time_slow_ship_->time_slow_rotation_comp = 1.0f;
  time_slow_ship_ = collector;
  time_slow_ship_->time_slow_rotation_comp = (float)kTimeSlowFactor;
  time_slow_ms_left_ = kTimeSlowWallMs / kTimeSlowFactor;
  time_slow_cue(true);
  NET_LOG("time slow started (%d sim ms, owner %s)\n", time_slow_ms_left_,
          !players->empty() && collector == players->front()->ship
              ? "player 1" : "player 2");
}

// See glgame.h: shared per-step countdown for both step loops.
void GLGame::time_slow_step() {
  if (time_slow_ms_left_ <= 0) return;
  time_slow_ms_left_ -= step_size;
  if (time_slow_ms_left_ <= 0) {
    time_slow_ms_left_ = 0;
    for (auto *gs : *players) gs->ship->time_slow_rotation_comp = 1.0f;
    time_slow_ship_ = NULL;
    time_slow_cue(false);
    NET_LOG("time slow ended\n");
  }
}

// See glgame.h: the engage dive / release sweep, refractory-deduped.
void GLGame::time_slow_cue(bool starting) {
  int *last = starting ? &time_slow_start_cue_ms_ : &time_slow_end_cue_ms_;
  if (current_time - *last < 1500) return;
  *last = current_time;
  Mix_Chunk *c = starting ? time_slow_start_sound : time_slow_end_sound;
  if (c != NULL) Mix_PlayChannel(-1, c, 0);
}

GLShip *GLGame::local_player() const {
  if (players->empty()) return NULL;
  return net_mode_ == NetClient ? players->back() : players->front();
}

GLShip *GLGame::remote_player() const {
  if (players->size() < 2) return NULL;
  return net_mode_ == NetClient ? players->front() : players->back();
}

// PROTO 25 seat lookups. Seats are 1-based wire ids stamped on every
// player ship at creation (Ship::net_seat); records key on them instead
// of list position so B4's sparse seats need no reader change.
GLShip *GLGame::player_by_seat(int seat) const {
  if (seat <= 0) return NULL;
  for (auto *gs : *players)
    if ((int)gs->ship->net_seat == seat) return gs;
  return NULL;
}

// The seat THIS machine's pilot sits in: the host is always 1, a client
// the seat WELCOME assigned (2 until B4 hands out 3..4). Meaningful in
// any mode — offline and replay both report 1 (P1's machine). The last
// live answer is CACHED: a client's session is deleted the moment the
// host is lost, and the provisional 2 mislabeled a seat-3/4 pilot's own
// HUD row (their badge on seat 2's score) for the whole rejoin window.
// A fresh session's WELCOME refreshes the cache through the live read.
int GLGame::net_local_seat() const {
  if (net_mode_ == NetClient) {
    NetSession *s = net_session();
    if (s) {
      net_local_seat_cache_ = s->player_id();
      return net_local_seat_cache_;
    }
    return net_local_seat_cache_ ? net_local_seat_cache_ : 2;
  }
  return 1;
}

// Client-side: the ship a relayed effect's header seat names (PROTO 25).
// Never this machine's own seat — its effects are already local — and a
// byte that resolves nowhere falls back to the host's ship, the only
// possible firer before B4's relays.
Ship *GLGame::net_firer_ship(int seat) const {
  if (seat != net_local_seat()) {
    GLShip *gs = player_by_seat(seat);
    if (gs) return gs->ship;
  }
  return players->empty() ? NULL : players->front()->ship;
}

// B5 (PB-D7): the spectate camera cycles over every OTHER player still in
// the game, not just the single 2P peer — the first living one in list
// order wins; when it dies the next living ship takes over by itself. A
// respawning ship (lives left, hull down) is the fallback so the camera
// doesn't snap away for a two-second respawn.
GLShip *GLGame::spectate_target() const {
  GLShip *local = local_player();
  GLShip *fallback = nullptr;
  for (GLShip *gs : *players) {
    if (gs == local) continue;
    // A parked seat's hull is stale-alive (frozen, shielded, pilotless) —
    // never a camera subject WHEN there could be a live alternative
    // (N>1). At one peer the parked hull is all there is, and the 2P
    // spectate-through-disconnect flow (spectate_disconnect.sh) keeps
    // its master behaviour. Host-side knowledge only: a client has no
    // roster entry for other seats and keeps the list-order pick (its
    // snapshots stop updating a parked hull, so the view is merely
    // static, not wrong — PB-D7 note for B6).
    if (net_peers_.size() > 1) {
      NetPeer *pw = net_peer_by_seat(gs->ship->net_seat);
      if (pw && (pw->lost || pw->parked)) continue;
    }
    if (gs->ship->is_alive()) return gs;
    if (!fallback && gs->ship->lives > 0) fallback = gs;
  }
  return fallback;
}

GLShip *GLGame::camera_target() const {
  if (is_spectating()) {
    if (GLShip *r = spectate_target()) return r;
  }
  return local_player();
}

// Arm/disarm the spectate countdown from replicated lives (host authoritative,
// mirrored on the client), so both roles agree on the timing. Purely a
// function of state + wall clock; called once per frame from either tick path.
void GLGame::update_spectate() {
  // Once game_over is latched (all players out, or a spectator who lost the
  // peer) there is no spectating — and after a peer disconnect the peer's
  // ship stays stale-alive, which would otherwise re-arm the countdown every
  // frame under the GAME OVER card.
  // NetReplay: a 2P replay shows both ghosts in split-screen; a dead ghost
  // is just history unfolding, never a spectate handoff.
  if (net_mode_ == NetOff || net_mode_ == NetReplay || players->size() < 2 ||
      game_over) {
    spectate_death_time_ = -1;
    return;
  }
  GLShip *local = local_player();
  bool local_out = local && !local->ship->is_alive() && local->ship->lives <= 0;
  // B5: ANY other player still in it keeps the spectate flow alive — the
  // camera hands between them as they fall (spectate_target).
  bool remote_in = spectate_target() != nullptr;
  if (local_out && remote_in) {
    if (spectate_death_time_ < 0) {
      spectate_death_time_ = current_time;
      NET_LOG("net: spectate armed\n");
    }
  } else {
    // Peer also out (GAME OVER takes over) or we came back (revived).
    if (spectate_death_time_ >= 0) NET_LOG("net: spectate ended\n");
    spectate_death_time_ = -1;
  }
}

bool GLGame::spectate_arming() const {
  return spectate_death_time_ >= 0 &&
         current_time - spectate_death_time_ < kSpectateDelayMs;
}

bool GLGame::is_spectating() const {
  return spectate_death_time_ >= 0 &&
         current_time - spectate_death_time_ >= kSpectateDelayMs;
}

int GLGame::spectate_countdown_secs() const {
  int remaining = kSpectateDelayMs - (current_time - spectate_death_time_);
  if (remaining < 0) remaining = 0;
  return (remaining + 999) / 1000;  // 5..1, ceil
}

void GLGame::touch_joystick(float nx, float ny) {
  if(!running || players->empty() || net_mode_ == NetReplay) return;
  local_player()->touch_joystick_input(nx, ny);
}

void GLGame::keyboard (unsigned char key, int x, int y) {
  // Board prompt/upload owns input on key-DOWN: record which nav keys were
  // actually PRESSED while the prompt is up, so keyboard_up can tell a
  // fresh press from a gameplay key released into the prompt (see there),
  // and swallow so a dead ship's input loop never runs under the card.
  // Ahead of the !running drop: a lost-link game over can land on a game
  // the host had paused, and the prompt must still answer there. (Never
  // live in a replay — board_maybe_start refuses NetReplay.)
  if (board_prompt_active()) {
    board_prompt_pressed_.insert(nav_key(key));
    return;
  }
  // The connection-lost card is next in rank, with the same down-tracking
  // (net_card_pressed_): keyboard_up exits only on a key PRESSED while the
  // card was up, so a fire key held through the disconnect and released
  // into the card can't throw a rejoinable session away. Also ahead of the
  // !running drop — the card shows on a paused game too (host paused, then
  // left), and a fresh press there must still arm the exit.
  if (net_card_owns_input()) {
    net_card_pressed_.insert(nav_key(key));
    return;
  }
  // Link healthy (or restored): presses recorded under a previous card are
  // stale — never let one confirm an exit on a later loss.
  if (!net_card_pressed_.empty()) net_card_pressed_.clear();
  if (!running)
    return;
  // Replay ghosts take no input — the records drive them.
  if (net_mode_ == NetReplay)
    return;

  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key);
  }
}

void GLGame::keyboard_up (unsigned char key, int x, int y) {
  const GeneralKeys &gk = g_prefs.general_keys;

  if (net_card_owns_input()) {
    // Once the auto-rejoin has constructed its lobby the hand-off is
    // committed: request_state_change here would overwrite next_state in
    // the one-frame window before the swap, orphaning that lobby (its
    // ctor already opened the room's joiner socket) and leaving the
    // armed net_handed_to_lobby_ skipping the credential release.
    if (net_handed_to_lobby_) return;
    // The leaderboard prompt outranks the card: a lost-link game over (the
    // spectator's host leaving) still starts the board flow, and its drawn
    // YES/NO prompt must answer — not sit dead while confirm quietly
    // destroys the upload by exiting. touch_tap already orders it this
    // way. Same fresh-press rule as the healthy-game block below.
    if (board_prompt_active()) {
      unsigned char nk = nav_key(key);
      if (board_prompt_pressed_.count(nk)) {
        board_prompt_pressed_.erase(nk);
        board_nav((char)nk);
      }
      return;
    }
    // The disconnect card carries a EXIT TO MENU row, so it answers like
    // every other menu: confirm activates the row, back leaves outright,
    // and nothing else does anything. Fire IS a confirm, so the touch
    // card's "TAP FIRE FOR MENU" still reads true. Only a key whose DOWN
    // happened while the card was up acts (net_card_pressed_, recorded in
    // keyboard()) — a fire key held through the disconnect and released
    // into the card used to throw a rejoinable session away. And when the
    // loss lands at game over (that card outranks this one's overlay),
    // the same 3 s grace as every other game-over exit applies.
    unsigned char nav = nav_key(key);
    if (net_card_pressed_.erase(nav) == 0) return;
    if (!is_exit_key(nav)) return;
    if (all_players_out() && game_over_grace_active()) return;
    request_state_change(new Menu());
    return;
  }

  // Replay playback controls (REPLAY.md R2): pause pauses, the time-scale
  // keys become playback speed (never a cheat — nothing is earned in a
  // replay), Esc exits to the menu, and everything else is swallowed so no
  // gameplay/cheat key can touch the recorded world. After the recording's
  // game over, any key exits (mirrors the offline game-over flow).
  if (net_mode_ == NetReplay) {
    // The finished-replay card draws the shared EXIT TO MENU row, so it
    // answers like one; the menu key stays a direct shortcut mid-playback.
    if (key == (unsigned char)gk.menu ||
        (replay_exit_offered() && is_exit_key(nav_key(key)))) {
      request_state_change(new Menu());
      return;
    }
    if (key == (unsigned char)gk.pause) { toggle_pause(); return; }
    // Paused, the pause menu owns w/s and confirm — the same ladder the
    // offline pause screen uses, so the drawn rows answer here too.
    if (pause_menu_active()) {
      unsigned char nav = nav_key(key);
      if (MenuSelect::is_up(nav) || MenuSelect::is_down(nav) ||
          MenuSelect::is_confirm(nav)) {
        pause_nav(nav);
        return;
      }
    }
    if (key == (unsigned char)gk.time_speed_up && replay_speed_ < 4.0f)
      replay_speed_ *= 2.0f;
    if (key == (unsigned char)gk.time_slow_down && replay_speed_ > 0.26f)
      replay_speed_ *= 0.5f;
    if (key == (unsigned char)gk.time_reset) replay_speed_ = 1.0f;
    return;
  }

  // The seat roster owns input while it is up: Esc backs out to the pause
  // menu instead of quitting the game, and left/right rebind a seat. The
  // pause key still resumes outright (toggle_pause closes the roster).
  if (roster_open()) {
    if (key == (unsigned char)gk.pause) { toggle_pause(); return; }
    roster_nav(nav_key(key));
    return;
  }

  // Paused with the menu up: w/s (and the arrows) move the highlight,
  // Enter/space confirm. The pause and menu keys are checked first so they
  // keep working as direct shortcuts even if someone has bound one of them
  // onto a nav key. Swallowing the rest can't latch a control: pausing
  // force-releases everything (toggle_pause) and key-downs are already
  // dropped while paused.
  if (pause_menu_active() && key != (unsigned char)gk.pause &&
      key != (unsigned char)gk.menu) {
    unsigned char nav = nav_key(key);  // arrows navigate like WASD
    if (MenuSelect::is_up(nav) || MenuSelect::is_down(nav) ||
        MenuSelect::is_confirm(nav)) {
      pause_nav(nav);
      return;
    }
  }

  // Host-only / debug keys are ignored on the online client — it never
  // mutates world state locally (the host's snapshots are authoritative).
  bool host_keys = net_mode_ != NetClient;

  if (host_keys && key == (unsigned char)gk.skip_level) {
      // Cheat: suppress all achievement unlocks for the rest of this game
      // (XR-057, ACHIEVEMENTS.md §1).
      Achievements::note_cheat_used();
      level_cleared = true;
      time_until_next_generation = 0;
      while(!objects->empty()) {
        delete objects->back();
        objects->pop_back();
      }
      while(!dead_objects->empty()) {
        delete dead_objects->back();
        dead_objects->pop_back();
      }
      Asteroid::num_killable = 0;
      // Level completion now also requires the hazards to be gone, so the
      // skip-level cheat must clear them too or the clear never triggers.
      while(!hazards->empty()) {
        delete hazards->back();
        hazards->pop_back();
      }
      // The grid still holds pointers to the asteroids just deleted, and
      // the next re-population is a sim tick away — which never comes
      // while paused. Anything probing the grid in between (the rejoin
      // path's safe_position when player 2 reconnects to a paused host)
      // walked dangling pointers: Glenn's skip-while-paused segfault.
      grid.update((std::list<Object *>*)objects);
  }

  if (host_keys && key == (unsigned char)gk.toggle_friendly_fire)
    host_toggle_friendly_fire();
  if (key == (unsigned char)gk.toggle_debug_grid) {
    debug_grid = !debug_grid;
  }
  // Time-scale changes are cheats too (XR-057); resetting to normal speed is
  // not itself a cheat, but any earlier change already suppressed this
  // generation and the rebuild re-checks time_between_steps != step_size.
  if (host_keys && key == (unsigned char)gk.time_speed_up && time_between_steps > 1) {
    time_between_steps--;
    Achievements::note_cheat_used();
  }
  if (host_keys && key == (unsigned char)gk.time_slow_down) {
    time_between_steps++;
    Achievements::note_cheat_used();
  }
  if (host_keys && key == (unsigned char)gk.time_reset) time_between_steps = step_size;
  if (key == (unsigned char)gk.pause) toggle_pause();
#if !defined(__ANDROID__) && !defined(__IOS__)
  // Enter joins the P2 seat only (FOURPLAYER.md D3) — P3/P4 are
  // controller-first, and the keyboard has no third layout to hand out.
  if (key == (unsigned char)gk.add_player2 && players->size() < 2)
    add_local_player(NULL, /*with_keys=*/true);
#endif
  // A live board prompt/upload OWNS all game-over input, including the menu
  // key (Esc). Only a key whose DOWN happened while the prompt was up acts
  // (recorded in keyboard()); a gameplay key still held at death and
  // released INTO the prompt is ignored — otherwise a held fire key would
  // silently confirm the YES-default upload and a held thrust key would
  // flip the selection. Everything else here is swallowed so the exit
  // paths below never run while the prompt owns the card.
  if (board_prompt_active()) {
    unsigned char nk = nav_key(key);
    if (board_prompt_pressed_.count(nk)) {
      board_prompt_pressed_.erase(nk);
      board_nav((char)nk);
    }
    return;
  }
  // The GAME OVER screen draws the shared EXIT TO MENU row, so it answers
  // like one: confirm or back, never "any key" — a stray press used to eat
  // the score screen. The short delay still stops the last shoot input from
  // skipping it. The menu key keeps its own path below.
  if (key != (unsigned char)gk.menu) {
    bool all_game_over = !players->empty();
    for (auto* glship : *players) {
      if (glship->ship->is_alive() || glship->ship->lives > 0) {
        all_game_over = false;
        break;
      }
    }
    if (all_game_over) {
      if (!is_exit_key(nav_key(key))) return;
      if (game_over_grace_active())
        return;
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      request_state_change(new Menu());
      return;
    }
  }
  if (key == (unsigned char)gk.menu) {
    // Online, Esc is not a quit: leaving mid-game tears down or abandons a
    // room other people are in, so the exit must be a chosen row, not a
    // reflex key. Esc opens the pause menu (RESUME / EXIT TO MENU) and,
    // with the menu already up, backs out of it — resume — like Esc on
    // every other screen; EXIT TO MENU is the deliberate way out. Game
    // over falls through: nothing is left to pause (toggle_pause would
    // refuse anyway) and Esc keeps meaning leave.
    if ((net_mode_ == NetHost || net_mode_ == NetClient) &&
        !all_players_out()) {
      toggle_pause();
      return;
    }
    save_progress();
    request_state_change(new Menu());
  }

  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key, false);
  }
}
