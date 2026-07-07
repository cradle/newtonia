#include "glgame.h"
#include "asset_path.h"
#include "highscore.h"
#include "preferences.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "glship.h"
#include "weapon/default.h"
#include "net_signal.h"
#include "net_transport.h"
#include "glcar.h"
#include "glstarfield.h"
#include "wrapped_point.h"
#include "intro.h"
#include "menu.h"
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
#include "teleport.h"
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

static void set_player_keys(GLShip *gs, int player_index) {
  const PlayerKeys &k = (player_index == 0) ? g_prefs.p1_keys : g_prefs.p2_keys;
  gs->set_keys(k.left, k.right, k.thrust, k.shoot, k.reverse, k.mine,
               k.next_weapon, k.boost, k.teleport, k.help, k.next_secondary,
               k.toggle_rotate_view);
  gs->set_keyboard_sensitivity(k.keyboard_sensitivity);
  gs->set_camera_smoothing(k.camera_smoothing);
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

GLGame::GLGame(SDL_GameController *controller) :
  State(),
  world(Point(default_world_width, default_world_height)),
  current_time(0),
  running(true),
  level_cleared(false),
  friendly_fire(g_prefs.friendly_fire),
  debug_grid(false),
  score_saved(false),
  game_over_time(-1),
  grid(Grid(world, Point(Asteroid::max_radius*2,Asteroid::max_radius*2))) {
  time_between_steps = step_size;

  enemies = new std::list<GLShip*>;
  players = new std::list<GLShip*>;
  ship_objects = new std::list<Object*>;
  objects = new std::list<Asteroid*>;
  dead_objects = new std::list<Asteroid*>;
  pickups = new std::list<Pickup*>;
  black_holes = new std::list<BlackHole*>;

  net_clear_event_outboxes();

  WrappedPoint::set_boundaries(world);


  starfield = new GLStarfield(world, star_density_scale());
  warp_pass_ = new WarpPass();

  time_until_next_step = 0;
  num_frames = 0;
  last_draw_time_ = SDL_GetTicks();

  generation = 0;
  Asteroid::num_killable = 0;
  add_asteroids();
  grid.update((std::list<Object *>*)objects);

  GLShip *object = new GLShip(grid, true);
  set_player_keys(object, 0);
  if(controller != NULL) {
    object->set_controller(controller);
  }
  object->ship->set_missile_asteroids((std::list<Object*>*)objects);
  ship_objects->push_back(object->ship);
  object->ship->set_missile_ships(ship_objects);
  object->ship->set_black_holes(black_holes);
  players->push_back(object);

  station = NULL;//new GLStation(enemies, players);
  mini_station = NULL;

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
}

GLGame::GLGame(NetSession *session, SDL_GameController *controller)
  : GLGame(controller) {
  net_mode_ = NetHost;
  net_session_ = session;
  // A fresh game's player 1 starts dead (offline you wait out the initial
  // countdown or tap fire). Online the host just finished the lobby, so
  // start alive — and without this the client's bootstrap snapshot catches
  // the corpse mid-countdown: the restore resurrects the ghost, the first
  // extras kill it again, and the joiner watches player 1 "die" at start.
  players->front()->ship->respawn(grid, false);
  players->front()->ship->bullets.clear();  // no lethal spawn-flash debris
  add_remote_player();
  NET_LOG("net: ice path %s\n",
         net_session_->transport()->connection_info().c_str());
  // Co-op scoring parity: the initial hull costs a life, so each co-op
  // player fields the same total ship count as a solo run — otherwise a
  // pair banks two free hulls and the high scores aren't comparable.
  // Host-authoritative; the client mirrors lives via the snapshots.
  for (auto *gs : *players) gs->ship->lives -= 1;
  // The host's friendly-fire preference is the room rule; the client's HUD
  // shows its own preference until told otherwise.
  net_send_event(Net::EV_FRIENDLY_FIRE, friendly_fire ? 1u : 0u);
}

GLGame::~GLGame() {
  save_progress();
  if (net_mode_ == NetClient) Ship::net_quiet_respawn = false;
  // Leaving an online game: tell the peer (best effort — a hard close is
  // also detected via the channel-close path).
  if (net_session_ && !net_connection_lost_)
    net_send_event(Net::EV_BYE);
  delete net_session_;  // closes + deletes the transport
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
  score_saved(false),
  game_over_time(-1),
  grid(Grid(Point(save.world_x, save.world_y),
            Point(Asteroid::max_radius*2, Asteroid::max_radius*2))) {
  time_between_steps = step_size;
  time_until_next_generation = save.time_until_next_generation;

  enemies = new std::list<GLShip*>;
  players = new std::list<GLShip*>;
  ship_objects = new std::list<Object*>;
  objects = new std::list<Asteroid*>;
  dead_objects = new std::list<Asteroid*>;
  pickups = new std::list<Pickup*>;
  black_holes = new std::list<BlackHole*>;

  net_clear_event_outboxes();

  WrappedPoint::set_boundaries(world);

  starfield = new GLStarfield(world, star_density_scale());

  time_until_next_step = 0;
  num_frames = 0;
  last_draw_time_ = SDL_GetTicks();

  // Restore asteroids
  Asteroid::num_killable = 0;
  for (const auto &sa : save.asteroids) {
    Asteroid *a = new Asteroid(sa.invincible, sa.invisible, sa.reflective,
                               sa.teleporting, sa.quantum, sa.tough, sa.armoured);
    a->restore_state(sa);
    objects->push_back(a);
  }
  grid.update((std::list<Object*>*)objects);

  // Restore pickups
  for (const auto &sp : save.pickups) {
    WrappedPoint pos(sp.pos_x, sp.pos_y);
    switch (sp.type) {
      case Save::PickupType::Weapon:   pickups->push_back(new WeaponPickup(pos, sp.weapon_index)); break;
      case Save::PickupType::Mine:     pickups->push_back(new MinePickup(pos)); break;
      case Save::PickupType::GigaMine: pickups->push_back(new GigaMinePickup(pos)); break;
      case Save::PickupType::Missile:  pickups->push_back(new MissilePickup(pos)); break;
      case Save::PickupType::Shield:   pickups->push_back(new ShieldPickup(pos)); break;
      case Save::PickupType::GodMode:  pickups->push_back(new GodModePickup(pos)); break;
      case Save::PickupType::ExtraLife: pickups->push_back(new ExtraLife(pos)); break;
      case Save::PickupType::NovaCharge: pickups->push_back(new NovaChargePickup(pos)); break;
      default: break;
    }
  }

  // Restore black holes
  for (const auto &sbh : save.black_holes) {
    black_holes->push_back(new BlackHole(WrappedPoint(sbh.pos_x, sbh.pos_y)));
  }

  // Restore players — player 1 is GLShip, player 2+ is GLCar (matches add_player2)
  for (const auto &sp : save.players) {
    bool is_p1 = players->empty();
    GLShip *gs = is_p1 ? new GLShip(grid, true) : new GLCar(grid, true);
    set_player_keys(gs, is_p1 ? 0 : 1);
    if (controller != NULL && is_p1) {
      gs->set_controller(controller);
    }
    gs->ship->set_missile_asteroids((std::list<Object*>*)objects);
    ship_objects->push_back(gs->ship);
    gs->ship->set_missile_ships(ship_objects);
    gs->ship->set_black_holes(black_holes);
    gs->ship->restore_state(sp, grid);
    gs->snap_camera_to_heading();
    players->push_back(gs);
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
    station = new GLStation(grid, enemies, players, (std::list<Object*>*)objects);
    station->restore_state(save.station, grid);
  } else {
    station = NULL;
  }

  // Restore the roaming mini-station (position + drift direction) exactly as it
  // was saved. If it had already been destroyed there is none to restore — the
  // next generation will spawn a fresh one as usual.
  if (save.mini_station.present && save.mini_station.alive) {
    mini_station = new GLMiniStation(grid, players, (std::list<Object*>*)objects);
    mini_station->restore_state(save.mini_station);
  } else {
    mini_station = NULL;
  }
  warp_pass_ = new WarpPass();

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
}

void GLGame::save_progress() {
  // Online play never touches the local solo save (see NETPLAY.md): the
  // hosted world is not the solo game, and the game-over delete below
  // would otherwise wipe real progress.
  if (net_mode_ != NetOff) return;
  if (score_saved) return;
  for (auto* gs : *players) {
    if (gs->ship->is_alive() || gs->ship->lives > 0) {
      Save::save_game(build_save_data());
      return;
    }
  }
  // All players dead with no lives remaining — game over, delete any save
  if (!save_deleted_) {
    Save::delete_save();
    save_deleted_ = true;
  }
}

Save::GameState GLGame::build_save_data(bool include_asteroids) const {
  Save::GameState s;
  s.generation                 = generation;
  s.world_x                    = world.x();
  s.world_y                    = world.y();
  s.level_cleared              = level_cleared;
  s.time_until_next_generation = time_until_next_generation;
  s.current_time               = current_time;

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
    } else {
      continue; // unknown pickup type, skip
    }
    s.pickups.push_back(sp);
  }

  for (auto* bh : *black_holes)
    s.black_holes.push_back({bh->position.x(), bh->position.y()});

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

void GLGame::maybe_start_intro() {
  // No Intro states online: both machines must keep ticking in lockstep
  // with the snapshot stream (a 2 s banner replaces them — Phase 8).
  if (net_mode_ != NetOff) return;
  const char *name = NULL;
  Asteroid *display = NULL;
  Intro::Kind kind = Intro::ASTEROID;
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
    case 9:  if (!black_holes->empty()) { kind = Intro::BLACK_HOLE;   name = "BLACK HOLE"; }    break;
    case 10: if (mini_station != NULL)  { kind = Intro::MINI_STATION; name = "MINI STATION"; }  break;
    case 20: if (station != NULL)       { kind = Intro::STATION;      name = "ENEMY STATION"; } break;
    default: return;
  }
  if (name == NULL) return;

  // The intro adopts this state (ownership transfers, so the StateManager
  // won't delete it) and hands it back when the player presses fire.
  request_state_change(new Intro(this, kind, name, display), true);
}

void GLGame::toggle_pause(bool broadcast) {
  running = !running;
  // Online, pausing is shared state: tell the peer (unless this call IS
  // the peer's event being applied).
  if (broadcast && net_mode_ != NetOff && net_session_ && !net_connection_lost_)
    net_send_event(running ? Net::EV_RESUME : Net::EV_PAUSE);
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
      pause_music_channel = Mix_PlayChannel(-1, pause_music_sound, -1);
    }
  }
}

bool GLGame::back_pressed() {
  save_progress();
  request_state_change(new Menu());
  return true;
}

void GLGame::focus_lost() {
  save_progress();
  // Online the sim must keep running while unfocused — the peer's game
  // doesn't stop. Sound still mutes below.
  if(running && net_mode_ == NetOff) {
    toggle_pause();
    auto_paused = true;
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
  // Assign to the first player who has no controller
  for(auto* glship : *players) {
    if(!glship->has_controller()) {
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

void GLGame::add_player2(SDL_GameController *ctrl) {
  if(net_mode_ != NetOff) return;  // player 2 is the remote peer online
  if(players->size() >= 2) return;
  Ship* p1 = players->front()->ship;
  if(!p1->is_alive() && !p1->lives) return;
  GLShip* object = new GLCar(grid, true);
  object->set_controller(ctrl);
  object->ship->set_missile_asteroids((std::list<Object*>*)objects);
  ship_objects->push_back(object->ship);
  for(auto *p : *players) p->ship->set_missile_ships(ship_objects);
  object->ship->set_missile_ships(ship_objects);
  object->ship->set_black_holes(black_holes);
  players->push_back(object);
}

// Player 2 for online play: same wiring as add_player2 but with no local
// controller or key bindings — the peer drives it via INPUT messages.
void GLGame::add_remote_player() {
  if(players->size() >= 2) return;
  GLShip* object = new GLCar(grid, true);
  object->ship->set_missile_asteroids((std::list<Object*>*)objects);
  ship_objects->push_back(object->ship);
  for(auto *p : *players) p->ship->set_missile_ships(ship_objects);
  object->ship->set_missile_ships(ship_objects);
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
}

// Elastic asteroid-asteroid collisions: 2D impulse physics (mass ~
// radius^2) plus a positional push that resolves overlap, for every live
// pair where at least one is elastic (reflective asteroids carry
// elastic=true). Pairs are processed once via inner iterator starting
// after outer. ONE definition for two callers: the host simulates it for
// real (announce=true: bounce ting + EV_ROID_BOUNCE), and the net client
// mirrors it silently each visual step — bounces are position-and-
// pairing-dependent, so without the mirror every one of them was a
// surprise the authoritative records corrected 100 ms later (the last
// source of asteroid jitter; gravity is mirrored the same way).
void GLGame::elastic_asteroid_collisions(bool announce) {
  std::list<Asteroid*>::iterator ai, bi;
  for(ai = objects->begin(); ai != objects->end(); ++ai) {
    Asteroid *a = *ai;
    if(!a->alive) continue;
    bi = ai; ++bi;
    for(; bi != objects->end(); ++bi) {
      Asteroid *b = *bi;
      if(!b->alive) continue;
      if(!a->elastic && !b->elastic) continue;

      // Use world-wrap aware distance: get closest copy of A to B
      Point a_near = a->position.closest_to(b->position);
      float dx = a_near.x() - b->position.x();
      float dy = a_near.y() - b->position.y();
      float dist2 = dx * dx + dy * dy;
      float sum_r = a->radius + b->radius;
      if(dist2 >= sum_r * sum_r) continue; // no overlap

      float dist = sqrtf(dist2);
      if(dist < 1e-4f) continue; // degenerate overlap, skip

      // Collision normal pointing from B to A
      float nx = dx / dist;
      float ny = dy / dist;

      // Positional correction: always push apart to resolve overlap,
      // including the case where children spawn inside each other.
      float overlap = sum_r - dist;
      float push = overlap * 0.5f + 0.5f;
      a->position += Point(nx, ny) * push;
      a->position.wrap();
      b->position += Point(-nx, -ny) * push;
      b->position.wrap();

      // Velocity impulse: only when approaching (negative = approaching)
      float vrel_n = (a->velocity.x() - b->velocity.x()) * nx
                   + (a->velocity.y() - b->velocity.y()) * ny;
      if(vrel_n >= 0.0f) continue; // already separating, no impulse needed

      // Mass proportional to area (radius^2)
      float ma = a->radius * a->radius;
      float mb = b->radius * b->radius;
      float impulse = -2.0f * vrel_n * ma * mb / (ma + mb);

      a->velocity = a->velocity + Point(nx, ny) * (impulse / ma);
      b->velocity = b->velocity - Point(nx, ny) * (impulse / mb);

      // Play a deep metallic ting when an asteroid strikes a reflective one,
      // but only if the collision is visible to any player.
      if(announce && (a->reflective || b->reflective) &&
         Asteroid::asteroid_ting_sound != NULL) {
        Point contact(
          (a->position.x() + b->position.x()) * 0.5f,
          (a->position.y() + b->position.y()) * 0.5f);
        float vol = sound_volume_for_point(contact);
        if(vol > 0.0f) {
          static Uint32 last_asteroid_ting_tick = UINT32_MAX;
          Uint32 now = SDL_GetTicks();
          if(now - last_asteroid_ting_tick >= 125) {
            last_asteroid_ting_tick = now;
            Mix_VolumeChunk(Asteroid::asteroid_ting_sound, (int)(MIX_MAX_VOLUME * vol));
            Mix_PlayChannel(-1, Asteroid::asteroid_ting_sound, 0);
            net_send_event(Net::EV_ROID_BOUNCE, (uint32_t)(vol * 255.0f));
          }
        }
      }
    }
  }
}

// ---- RTT probe (PROTO 11) ----------------------------------------------
// 1 Hz MSG_PING on the UNRELIABLE channel; the peer echoes the timestamp
// back as MSG_PONG untouched, so only the sender's clock is ever read —
// no cross-machine clock comparison. A lost probe just skips a sample.

void GLGame::net_ping_tick(int delta) {
  net_ping_timer_ += delta;
  if (net_ping_timer_ < 1000) return;
  net_ping_timer_ = 0;
  std::vector<uint8_t> p;
  Net::put_header(p, Net::MSG_PING, net_mode_ == NetHost ? 1 : 2);
  Net::put_u32(p, (uint32_t)SDL_GetTicks());
  net_session_->transport()->send_unreliable(&p[0], p.size());
}

bool GLGame::net_handle_ping_pong(uint8_t msg_type, Net::Reader &r) {
  if (msg_type == Net::MSG_PING) {
    uint32_t t = r.u32();
    if (!r.ok) return true;
    std::vector<uint8_t> p;
    Net::put_header(p, Net::MSG_PONG, net_mode_ == NetHost ? 1 : 2);
    Net::put_u32(p, t);
    net_session_->transport()->send_unreliable(&p[0], p.size());
    return true;
  }
  if (msg_type == Net::MSG_PONG) {
    uint32_t t = r.u32();
    if (!r.ok) return true;
    // uint32 subtraction survives SDL_GetTicks wrap; anything over 10 s
    // is a thawed process or a stalled relay, not a latency reading.
    float sample = (float)(uint32_t)((uint32_t)SDL_GetTicks() - t);
    if (sample < 10000.0f) {
      if (net_rtt_ms_ < 0.0f) NET_LOG("net: rtt %.0f ms (first pong)\n", sample);
      net_rtt_ms_ = net_rtt_ms_ < 0.0f ? sample
                                       : net_rtt_ms_ * 0.8f + sample * 0.2f;
      net_rtt_ring_[net_rtt_ring_i_] = sample;
      net_rtt_ring_i_ = (net_rtt_ring_i_ + 1) % 8;
      if (net_rtt_ring_n_ < 8) net_rtt_ring_n_++;
    }
    return true;
  }
  return false;
}

float GLGame::net_lead_ms() const {
  // Minimum of the recent samples, not the smoothed average: a spike is
  // relay queueing, not path length, and the smoothed value stays wrong
  // for ~10 s after one while every extrapolation target overshoots.
  if (net_rtt_ring_n_ == 0) return 0.0f;
  float rtt = net_rtt_ring_[0];
  for (int i = 1; i < net_rtt_ring_n_; i++)
    if (net_rtt_ring_[i] < rtt) rtt = net_rtt_ring_[i];
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

// Asteroid flavour (sim_exact reconcile): the sim pose already rides the
// authority — only the drawn-continuity offset fades.
void net_decay_render_offset(Object &o, int delta) {
  float ex = o.net_pose_err.x(), ey = o.net_pose_err.y();
  if (ex * ex + ey * ey < 0.25f) {
    o.net_pose_err = Point(0.0f, 0.0f);
    return;
  }
  o.net_pose_err = o.net_pose_err * expf(-(float)delta / 65.0f);
}
}  // namespace

void GLGame::net_host_poll() {
  NetTransport *t = net_session_->transport();
  Ship *remote = players->size() >= 2 ? players->back()->ship : NULL;

  std::vector<unsigned char> msg;
  while (t->poll(msg)) {
    Net::Reader r(msg.empty() ? nullptr : &msg[0], msg.size());
    Net::Header h;
    if (!Net::read_header(r, h)) continue;
    if (net_handle_ping_pong(h.msg_type, r)) continue;
    if (h.msg_type == Net::MSG_EVENT) {
      uint8_t code = r.u8();
      uint32_t arg = r.remaining() >= 4 ? r.u32() : 0;
      if (r.ok) net_handle_event(code, arg);
      continue;
    }
    if (h.msg_type != Net::MSG_INPUT) continue;

    Net::InputState in;
    if (!Net::decode_input(r, in)) continue;
    // Unreliable channel: drop stale/reordered packets (signed distance
    // handles seq wrap).
    if (net_have_input_ && (int32_t)(in.seq - net_last_input_seq_) <= 0)
      continue;
    net_last_input_seq_ = in.seq;
    // An INPUT blackout (unreliable channel dying under relay congestion)
    // is what big local-ship corrections on the CLIENT look like from
    // here — log the gap so the two logs can be correlated.
    if (net_have_input_ && current_time - net_last_input_time_ > 300)
      NET_LOG("net: input gap %d ms ended (seq %u)\n",
              current_time - net_last_input_time_, in.seq);
    net_last_input_time_ = current_time;
    net_input_zeroed_ = false;
    if (!remote) continue;

    if (!net_have_input_) {
      // First INPUT: baseline the one-shot counters instead of firing
      // whatever the client accumulated before we were listening.
      net_have_input_ = true;
      net_prev_boost_ = in.boost_count;
      net_prev_next_weapon_ = in.next_weapon_count;
      net_prev_next_secondary_ = in.next_secondary_count;
      net_prev_teleport_ = in.teleport_count;
      net_prev_respawn_ = in.respawn_count;
      net_prev_shoot_press_ = in.shoot_press_count;
      net_prev_secondary_press_ = in.secondary_press_count;
      // The first INPUT proves the client's game is up: if we are
      // paused (auto-paused on its disconnect, or by hand), share the
      // pause state now — an event sent while it was still in the lobby
      // would have been dropped.
      if (!running) net_send_event(Net::EV_PAUSE);
    }

    // One-shot deltas; capped so a rejoining/wrapped counter can't burst.
    uint8_t boosts = (uint8_t)(in.boost_count - net_prev_boost_);
    uint8_t weapons = (uint8_t)(in.next_weapon_count - net_prev_next_weapon_);
    uint8_t secondaries =
        (uint8_t)(in.next_secondary_count - net_prev_next_secondary_);
    uint8_t teleports = (uint8_t)(in.teleport_count - net_prev_teleport_);
    uint8_t respawns = (uint8_t)(in.respawn_count - net_prev_respawn_);
    uint8_t shot_presses = (uint8_t)(in.shoot_press_count - net_prev_shoot_press_);
    uint8_t sec_presses =
        (uint8_t)(in.secondary_press_count - net_prev_secondary_press_);
    net_prev_shoot_press_ = in.shoot_press_count;
    net_prev_secondary_press_ = in.secondary_press_count;
    net_prev_boost_ = in.boost_count;
    net_prev_next_weapon_ = in.next_weapon_count;
    net_prev_next_secondary_ = in.next_secondary_count;
    net_prev_teleport_ = in.teleport_count;
    net_prev_respawn_ = in.respawn_count;

    if (!remote->is_alive()) {
      // Dead ships take no control input, same as the local player. Keys
      // still held at death stay suppressed through the respawn until the
      // player releases and re-presses them — respawn's reset() gives the
      // local player exactly that restriction.
      net_held_suppress_ = 0xffff;
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
    net_held_suppress_ &= in.held;
    uint16_t held = in.held & (uint16_t)~net_held_suppress_;

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
    if (shot_presses)
      remote->shoot(true);
    else if (!(held & Net::IN_SHOOT))
      remote->shoot(false);
    if (sec_presses)
      remote->fire_secondary(true);
    else if (!(held & Net::IN_SECONDARY))
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
  if (remote && net_have_input_ && !net_input_zeroed_ &&
      current_time - net_last_input_time_ > 1000) {
    net_input_zeroed_ = true;
    remote->rotate_left(false);
    remote->rotate_right(false);
    remote->thrust(false);
    remote->reverse(false);
    remote->shoot(false);
    remote->fire_secondary(false);
  }
}

void GLGame::net_adopt_signal(NetSignal *signal, const std::string &room_code,
                              const std::vector<std::string> &ice_servers,
                              const std::string &room_token) {
  net_signal_ = signal;
  net_room_code_ = room_code;
  net_room_token_ = room_token;
  net_ice_ = ice_servers;
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
    case NetSignal::Event::Closed:
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
      // no-such-room / expired on reclaim: the room is gone for good.
      NET_LOG("net: room %s lost (%s)\n", net_room_code_.c_str(),
             ev.text.c_str());
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
  net_host_signal_reclaim_tick(delta);

  NetSignal::Event ev;
  while (net_signal_->poll(ev)) {
    NetSignalEventResult common = net_host_signal_common_event(ev);
    if (common == NetSigDropped) return;
    if (common == NetSigHandled) continue;
    switch (ev.kind) {
      case NetSignal::Event::Room:
        NET_LOG("net: room %s reclaimed\n", net_room_code_.c_str());
        break;
      case NetSignal::Event::PeerJoin:
        // The client re-entered the room: its transport is dead even if
        // ours has not noticed yet. Enter the rejoin flow immediately.
        NET_LOG("net: peer rejoined the room - fast loss detect\n");
        delete net_session_;
        net_session_ = nullptr;
        net_connection_lost_ = true;
        return;  // net_host_rejoin_poll owns the signal from here
      default:
        break;
    }
  }
}

// Client gone but the room is still open: keep simulating solo, offer a
// fresh transport through the room, and resume when the peer rejoins
// (a rejoin is a plain JOIN on their side — full snapshot bootstrap).
void GLGame::net_host_rejoin_poll(int delta) {
  Ship *remote = players->size() >= 2 ? players->back()->ship : NULL;

  // Runs once per loss: only while there is no live rehost offer AND no
  // session other than the dead one (a fresh session created from a
  // rejoin answer is Handshaking on a HEALTHY transport — leave it be).
  if (!net_rehost_ &&
      (!net_session_ || net_session_->transport()->failed())) {
    // First tick after the loss: park the remote ship and re-host. An
    // alive ship stays visible where it stood, motionless with the
    // shield up (invincible), until its owner rejoins; a dead one keeps
    // its corpse frozen (no respawn countdown bleeding lives into
    // drifting asteroids). Held inputs are cleared in case the dead-man
    // switch hadn't zeroed them yet.
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
    delete net_session_;
    net_session_ = nullptr;
    net_rehost_ = NetTransport::create();
    net_rehost_offer_sent_ = false;
    if (net_rehost_) {
      net_rehost_->set_ice_servers(net_ice_);
      net_rehost_->set_trickle(true);  // the room relay carries candidates
      net_rehost_->start_host();
    }
    NET_LOG("net: player 2 lost - room %s reopened for rejoin\n",
           net_room_code_.c_str());
    // Pause rather than playing on solo: waiting is the sensible default
    // while the room is open for a rejoin (the pause key still resumes a
    // solo session by hand). No broadcast — there is no peer to tell.
    if (running) {
      toggle_pause(false);
      NET_LOG("net: paused awaiting rejoin\n");
    }
  }

  // Signal socket dropped mid-rejoin: reclaim the room (M3-1) with the
  // same countdown the healthy path uses; the offer re-sends once the
  // reclaimed Room frame arrives.
  net_host_signal_reclaim_tick(delta);

  if (net_rehost_ && !net_rehost_offer_sent_ && net_signal_retry_ms_ <= 0 &&
      net_rehost_->local_description_ready()) {
    net_signal_->send_offer(net_rehost_->local_description());
    net_rehost_offer_sent_ = true;
  }

  // Trickle ICE (M3-2b): stream the rehost transport's candidates to the
  // rejoining client through the room — only once the offer is out (a
  // candidate arriving before it gets wiped with the relay's stale-cand
  // buffer when the offer lands).
  if (net_signal_retry_ms_ <= 0 && (net_rehost_offer_sent_ || net_session_)) {
    NetTransport *t =
        net_rehost_ ? net_rehost_
                    : (net_session_ ? net_session_->transport() : nullptr);
    std::string c;
    while (t && t->poll_local_candidate(c)) {
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
      net_rehost_->set_remote_answer(ev.text);
      net_session_ = new NetSession(net_rehost_, NetSession::HostRole);
      net_rehost_ = nullptr;
    } else if (ev.kind == NetSignal::Event::Cand) {
      NetTransport *t =
          net_rehost_ ? net_rehost_
                      : (net_session_ ? net_session_->transport() : nullptr);
      if (t) t->add_remote_candidate(ev.text2, ev.text);
    } else if (ev.kind == NetSignal::Event::Room) {
      // Reclaimed mid-rejoin: the room's stored offer died with the old
      // socket — resend the current one.
      NET_LOG("net: room %s reclaimed (mid-rejoin)\n", net_room_code_.c_str());
      net_rehost_offer_sent_ = false;
    }
  }

  // Fresh session handshaking (HELLO/WELCOME) over the new transport.
  if (net_session_) {
    net_session_->update(delta);
    if (net_session_->phase() == NetSession::Ready) {
      net_connection_lost_ = false;
      net_have_input_ = false;      // re-baseline the one-shot counters
      net_input_zeroed_ = false;
      net_rtt_ms_ = -1.0f;          // fresh transport, fresh RTT baseline
      net_ping_timer_ = 0;
      net_rtt_ring_n_ = 0;
      net_rtt_ring_i_ = 0;
      net_held_suppress_ = 0xffff;  // fresh presses required, like a spawn
      net_force_keyframe_ = true;   // rejoined client starts from a keyframe
      net_last_input_time_ = current_time;
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
      net_banner_text_ = "PLAYER 2 RECONNECTED";
      // Re-sync the room rule — the rejoiner may be a fresh app launch
      // whose HUD reset to its own preference.
      net_send_event(Net::EV_FRIENDLY_FIRE, friendly_fire ? 1u : 0u);
      NET_LOG("net: ice path %s\n",
             net_session_->transport()->connection_info().c_str());
      // Host paused (auto-paused on the disconnect, or by hand): the
      // paused tick never reaches the 10 Hz send, so push the keyframe
      // NOW — the rejoiner's lobby is waiting on it to bootstrap the
      // world. The EV_PAUSE follows on the client's first INPUT, once
      // it provably exists (the lobby does not consume events).
      if (!running) {
        net_snapshot_timer_ = 100;
        net_host_send_snapshot(0);
      }
      NET_LOG("net: player 2 rejoined\n");
    } else if (net_session_->phase() == NetSession::Failed ||
               net_session_->phase() == NetSession::Rejected) {
      // Bad handshake (wrong build?): drop it and re-offer for another try.
      delete net_session_;
      net_session_ = nullptr;
      net_rehost_ = NetTransport::create();
      if (net_rehost_) net_rehost_->set_trickle(true);
      net_rehost_offer_sent_ = false;
      if (net_rehost_) {
        net_rehost_->set_ice_servers(net_ice_);
        net_rehost_->start_host();
      }
    }
  }
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
  for (const Particle &p : s.bullets) nx_write_projectile(out, p);
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
// order every apply).
void nx_write_enemy_bullets(Save::Stream &out, const std::list<GLShip *> *enemies) {
  nx_write(out, (uint16_t)enemies->size());
  for (auto *e : *enemies) {
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
  if (!net_session_ || net_connection_lost_) return;  // mid-rejoin
  net_snapshot_timer_ += delta;
  if (net_snapshot_timer_ < 100) return;
  net_snapshot_timer_ = 0;

  bool keyframe = net_force_keyframe_ || (net_slot_ % 10 == 0);
  net_slot_++;
  if (!keyframe && net_send_delta()) return;

  // KEYFRAME: the full snapshot, exactly as Milestone 1 sent every slot.
  Save::MemStream payload;
  Save::serialize_game(payload, build_save_data());

  nx_write(payload, (uint32_t)players->size());
  for (auto *gs : *players) nx_write_ship(payload, *gs->ship);
  nx_write_mini_station_bullets(payload, mini_station);
  nx_write_enemy_bullets(payload, enemies);

  nx_write(payload, (uint32_t)objects->size());
  for (auto *a : *objects) nx_write(payload, a->net_id);

  Net::send_snapshot(net_session_->transport(), ++net_snapshot_id_,
                     payload.data(), 1);
  net_force_keyframe_ = false;

  // The keyframe is the client's new baseline: deltas from here describe
  // changes against it (reliable ordered channel — no acks needed).
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

  // Bandwidth telemetry (M2-6): a line every 10 s of play at 10 Hz.
  net_bytes_sent_ += payload.data().size();
  if (net_slot_ % 100 == 0) {
    NET_LOG("net: slot #%d gen=%d asteroids=%d key_bytes=%d avg10s=%.1f KB/s\n",
           net_slot_, generation, (int)objects->size(),
           (int)payload.data().size(), net_bytes_sent_ / 10240.0f);
    net_bytes_sent_ = 0;
  }
}

bool GLGame::net_send_delta() {
  // Everything except asteroids rides wholesale — it is small and reuses
  // the entire keyframe apply path on the client. Asteroids are diffed
  // below, so skip capturing them into s.
  Save::GameState s = build_save_data(false);
  Save::MemStream payload;
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
        fabsf(a->position.x() - pred_x) > 50.0f ||
        fabsf(a->position.y() - pred_y) > 50.0f;
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
  // change) is not worth the format — send a keyframe this slot instead.
  if (payload.data().size() > Net::SNAPSHOT_CHUNK_BYTES) return false;

  std::vector<uint8_t> msg;
  msg.reserve(Net::HEADER_SIZE + 4 + payload.data().size());
  Net::put_header(msg, Net::MSG_DELTA, 1);
  Net::put_u32(msg, ++net_snapshot_id_);
  Net::put_bytes(msg, &payload.data()[0], payload.data().size());
  net_session_->transport()->send_reliable(&msg[0], msg.size());

  // Only now (the send is committed) does the baseline move.
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

  net_bytes_sent_ += msg.size();
  if (net_slot_ % 100 == 0) {
    NET_LOG("net: slot #%d gen=%d asteroids=%d delta_bytes=%d (new %d dyn %d rm %d) avg10s=%.1f KB/s\n",
           net_slot_, generation, (int)objects->size(), (int)msg.size(),
           (int)fresh.size(), (int)dyn.size(), (int)removed.size(),
           net_bytes_sent_ / 10240.0f);
    net_bytes_sent_ = 0;
  }
  return true;
}

// Host-side toggle (G key, or the touch region on the HUD text). The
// flip persists as the host's own preference and — online — is announced
// as the room rule. The client never reaches this (host_keys / the
// net_mode_ guard in touch_tap).
void GLGame::host_toggle_friendly_fire() {
  friendly_fire = !friendly_fire;
  g_prefs.friendly_fire = friendly_fire;
  save_preferences();
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
}

void GLGame::net_send_event(uint8_t code, uint32_t arg) {
  // While the joiner is disconnected (rejoinable loss) the host plays on
  // with no session at all — level completion and pause still fire events.
  if (!net_session_) return;
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_EVENT, net_mode_ == NetHost ? 1 : 2);
  Net::put_u8(msg, code);
  Net::put_u32(msg, arg);
  net_session_->transport()->send_reliable(&msg[0], msg.size());
}

void GLGame::net_handle_event(uint8_t code, uint32_t arg) {
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
      net_connection_lost_ = true;
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
      break;
    case Net::EV_FRIENDLY_FIRE: {
      // The host's preference is the room rule; adopt it for the HUD only
      // (damage runs in the host sim). g_prefs stays the player's own.
      bool on = arg != 0;
      if (on != friendly_fire) {
        net_banner_text_ = on ? "FRIENDLY FIRE ON" : "FRIENDLY FIRE OFF";
        net_banner_ms_ = 2000;
      }
      friendly_fire = on;
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
      if (snd) Mix_PlayChannel(-1, snd, 0);
      NET_LOG("net: roid impact\n");
      float ix, iy;
      Net::unpack_pos(arg, ix, iy, world.x(), world.y());
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
      break;
    case Net::EV_ROID_BOUNCE:
      // Asteroid-vs-reflective bounce; arg is the volume the host
      // computed for the nearest player.
      if (Asteroid::asteroid_ting_sound) {
        Mix_VolumeChunk(Asteroid::asteroid_ting_sound,
                        (int)(arg & 0xff) * MIX_MAX_VOLUME / 255);
        Mix_PlayChannel(-1, Asteroid::asteroid_ting_sound, 0);
      }
      break;
    case Net::EV_REMOTE_SHOT: {
      // The host player's gun, attenuated by distance to our ship. Our
      // own shots play locally; this event only ever describes P1.
      if (players->empty()) break;
      Ship *shooter = players->front()->ship;
      float vol = net_listener_volume(shooter->position);
      if (vol > 0.0f && shooter->shoot_sound) {
        Mix_VolumeChunk(shooter->shoot_sound, (int)(MIX_MAX_VOLUME * vol));
        Mix_PlayChannel(-1, shooter->shoot_sound, 0);
      }
      break;
    }
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
      Mix_Chunk *snd = NULL;
      if (code == Net::EV_WORLD_SHOT) snd = local_player()->ship->shoot_sound;
      else if (code == Net::EV_WORLD_BOOM) snd = local_player()->ship->explode_sound;
      else snd = station_explode_sound;
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
      int idx = (int)(arg & 0xff);
      GLShip *gs = NULL;
      if (idx == 1 && !players->empty()) gs = players->front();
      else if (idx == 2 && players->size() >= 2) gs = players->back();
      if (gs && gs->ship->is_alive()) {
        gs->ship->explode(gs->ship->position, gs->ship->velocity * 0.25f);
        if ((arg & 0x100) && Asteroid::ting_sound)
          Mix_PlayChannel(-1, Asteroid::ting_sound, 0);
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
}


// ---- client side ---------------------------------------------------------

GLGame::GLGame(const Save::GameState &snapshot, NetSession *session,
               SDL_GameController *controller)
  : GLGame(snapshot, (SDL_GameController *)NULL) {
  net_mode_ = NetClient;
  net_session_ = session;
  net_assembler_ = new Net::SnapshotAssembler();
  NET_LOG("net: ice path %s\n",
         net_session_->transport()->connection_info().c_str());
  // Snapshot restores call Ship::respawn 10x/s; without this its hum
  // start-then-halt leaks random audible blips. The snapshot extras are
  // the only hum authority on a client.
  Ship::net_quiet_respawn = true;

  // The save-restore base constructor bound player-1 keys to the FIRST ship,
  // but on the client the local player is the LAST one; the first is the
  // remote host's ship. Strip the ghost's bindings and give the local ship
  // this machine's player-1 controls.
  if (players->size() >= 2) {
    GLShip *remote = players->front();
    remote->set_keys(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    remote->set_controller(NULL);
    GLShip *local = players->back();
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

// quiet: suppress vanish explosions for this apply (level rollover wipes
// every projectile at once — that's a rebuild, not a barrage of booms).
bool nx_read_projectiles(Save::Stream &in, Ship &s, bool quiet) {
  uint16_t n = 0;
  float x, y, vx, vy;

  if (!nx_read(in, n)) return false;
  s.bullets.clear();
  for (int i = 0; i < n; i++) {
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    s.bullets.push_back(Particle(Point(x, y), Point(vx, vy), 2000.0f));
  }

  if (!nx_read(in, n)) return false;
  // Mines that disappear from the snapshot were detonated by the host
  // (proximity) — play the explosion here. Position-match against the
  // incoming set; mines barely drift, so 100 units is generous.
  std::vector<Particle> old_mines;
  old_mines.swap(s.mines);
  for (int i = 0; i < n; i++) {
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    s.mines.push_back(Particle(Point(x, y), Point(vx, vy), 60000.0f));
    for (size_t j = 0; j < old_mines.size(); j++) {
      if (old_mines[j].position.distance_to(WrappedPoint(x, y)) < 100.0f) {
        old_mines.erase(old_mines.begin() + j);
        break;
      }
    }
  }
  if (!quiet)
    for (auto &om : old_mines) s.net_mine_exploded(om.position, om.velocity);

  if (!nx_read(in, n)) return false;
  std::vector<Particle> old_gigas;
  old_gigas.swap(s.giga_mines);
  for (int i = 0; i < n; i++) {
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    s.giga_mines.push_back(Particle(Point(x, y), Point(vx, vy), 60000.0f));
    for (size_t j = 0; j < old_gigas.size(); j++) {
      if (old_gigas[j].position.distance_to(WrappedPoint(x, y)) < 100.0f) {
        old_gigas.erase(old_gigas.begin() + j);
        break;
      }
    }
  }
  if (!quiet)
    for (auto &og : old_gigas) s.net_giga_mine_exploded(og.position);

  if (!nx_read(in, n)) return false;
  // Missiles carry local presentation state the wire doesn't (trail,
  // thrust ramp): wholesale replacement wiped it 10x/s, so replicated
  // missiles looked like they teleported. Adopt it from the nearest
  // previous missile instead.
  std::vector<MissileShot> old_missiles;
  old_missiles.swap(s.missiles);
  for (int i = 0; i < n; i++) {
    float fx, fy, time_left;
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy) ||
        !nx_read(in, fx) || !nx_read(in, fy) || !nx_read(in, time_left)) return false;
    MissileShot m(WrappedPoint(x, y), Point(fx, fy), Point(0, 0));
    m.velocity = Point(vx, vy);
    m.time_left = time_left;
    int best = -1;
    float best_d = 150.0f;  // a missile moves well under this per delta
    for (size_t j = 0; j < old_missiles.size(); j++) {
      float d = old_missiles[j].position.distance_to(m.position);
      if (d < best_d) { best_d = d; best = (int)j; }
    }
    if (best >= 0) {
      m.trail = std::move(old_missiles[best].trail);
      m.thrust = old_missiles[best].thrust;
      m.sound_handle = old_missiles[best].sound_handle;
      old_missiles.erase(old_missiles.begin() + best);
    }
    s.missiles.push_back(m);
  }
  // A missile that vanished with life remaining exploded on the host
  // (collision) — the expiry case detonates locally in Ship::step. Play
  // the explosion here since the client never simulates the impact.
  for (auto &om : old_missiles) {
    if (!quiet && om.time_left > 300.0f)
      s.net_missile_exploded(om.position, om.velocity);
  }
  // Fly loop: locally-fired missiles get it from the weapon; replicated
  // ones share one channel per ship, kept alive via the adopted handles
  // and halted automatically when the last holder is destroyed.
  {
    std::shared_ptr<int> fly;
    for (auto &m : s.missiles)
      if (m.sound_handle) { fly = m.sound_handle; break; }
    for (auto &m : s.missiles) {
      if (!m.sound_handle) {
        if (!fly) fly = s.net_start_missile_fly_loop();
        m.sound_handle = fly;
      }
    }
  }

  if (!nx_read(in, n)) return false;
  size_t old_novas = 0;
  for (const Shockwave &w : s.shockwaves)
    if (w.is_nova) old_novas++;
  s.shockwaves.clear();
  size_t new_novas = 0;
  for (int i = 0; i < n; i++) {
    float px, py, radius, max_radius, speed, time_left;
    uint8_t is_nova;
    if (!nx_read(in, px) || !nx_read(in, py) || !nx_read(in, radius) ||
        !nx_read(in, max_radius) || !nx_read(in, speed) ||
        !nx_read(in, time_left) || !nx_read(in, is_nova)) return false;
    Shockwave w(Point(px, py), max_radius, speed, time_left, is_nova != 0);
    w.radius = radius;
    w.prev_radius = radius;
    s.shockwaves.push_back(w);
    if (is_nova) new_novas++;
  }
  // A nova wave the client hasn't seen yet: the wave itself replicates,
  // only its boom was host-side.
  if (!quiet && new_novas > old_novas) s.net_nova_arrived();
  return true;
}

}  // namespace

void GLGame::tick_net_client(int delta) {
  current_time += delta;
  // Anything the local kill() calls queued (ghost removals) is noise —
  // drop it; the host relays the real cues (or the client detects the
  // cosmetic ones itself).
  Ship::net_ship_impacts.clear();
  Ship::net_shots.clear();
  Ship::net_booms.clear();
  // Remote (host) ship: attenuate its self-played sounds (death
  // explosion, god-mode tics) by distance to the local ship.
  if (players->size() >= 2)
    players->front()->ship->sound_volume_scale =
        net_listener_volume(players->front()->ship->position);

  if (!net_connection_lost_) {
    net_client_poll();
    net_ping_tick(delta);
  }
  if (net_session_->transport()->failed()) net_connection_lost_ = true;
  if (net_connection_lost_) {
    // M3-1 auto-rejoin: with a room code the loss is recoverable — hand
    // the game back to a fresh auto-joining lobby (full keyframe
    // bootstrap, the same path as a manual rejoin). Any key/button still
    // exits to the menu via the input handlers.
    if (!net_room_code_.empty()) {
      if (net_client_rejoin_ms_ <= 0) net_client_rejoin_ms_ = 1500;
      net_client_rejoin_ms_ -= delta;
      if (net_client_rejoin_ms_ <= 0) {
        NET_LOG("net: auto-rejoining room %s\n", net_room_code_.c_str());
        request_state_change(new NetLobby(net_room_code_));
        net_room_code_.clear();  // fire once
      }
    }
    return;  // frozen; draw shows the reconnect notice
  }
  if (net_banner_ms_ > 0) net_banner_ms_ -= delta;

  if (!running) {
    last_tick += delta;
    return;
  }

  time_until_next_step -= delta;
  while (time_until_next_step <= 0) {
    // Visual/kinematic stepping only: motion, particles, trails, timers.
    // No collisions, kills, drops or generation logic — the host simulates
    // and its snapshots overwrite this extrapolation at 10 Hz.
    for (auto *bh : *black_holes) bh->step(step_size);
    for (auto *a : *objects) {
      a->step(step_size);
      net_decay_render_offset(*a, step_size);  // drawn pose fades to truth
    }
    // Mirror the host's asteroid gravity so drift between 1 Hz keyframes
    // stays small — otherwise every asteroid near a hole goes stale.
    for (auto *bh : *black_holes)
      for (auto *a : *objects)
        if (!a->invincible) bh->apply_gravity(*a, step_size);
    // Mirror the host's elastic bounces too (silently), or every one of
    // them is a surprise the records correct 100 ms later.
    elastic_asteroid_collisions(/*announce=*/false);
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
    for (auto *gs : *players) gs->step(step_size, grid);
    // The remote (host) ship reconciles like the asteroids; the LOCAL
    // ship never banks an error (its pose is authoritative, v12), so
    // this is a no-op for it.
    for (auto *gs : *players) net_smooth_step(*gs->ship, step_size);
    // v12: with pose authority the black hole must pull the pilot HERE —
    // the host's pull is overwritten by every adopted INPUT. Same
    // reduced-pull rule as the host's ship loop; the event-horizon kill
    // stays host-side (ignore the return — death arrives as a snapshot
    // alive-transition).
    {
      Ship *me = players->back()->ship;
      if (me->is_alive()) {
        float scale = (me->god_mode_time_remaining() > 0 ||
                       me->shield_active()) ? 0.25f : 1.0f;
        for (auto *bh : *black_holes)
          bh->apply_gravity(*me, step_size, scale);
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
    grid.update((std::list<Object *> *)objects);
    // Cosmetic bullet-vs-asteroid impacts (debris + thud/ting for
    // asteroids a bullet can't kill), detected locally against the fresh
    // grid — replaces the host's EV_ROID impact events (PROTO 10).
    for (auto *gs : *players) gs->ship->net_cosmetic_impacts(grid);

    net_client_send_input();
    time_until_next_step += time_between_steps;
  }

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

void GLGame::net_client_send_input() {
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
  Net::encode_input(msg, in, 2);
  net_session_->transport()->send_unreliable(&msg[0], msg.size());

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
    net_session_->transport()->send_reliable(&msg[0], msg.size());
    net_mirror_steps_ = 0;
    net_mirror_held_ = in.held;
    net_mirror_counts_ = counts;
  }
}

void GLGame::net_client_poll() {
  NetTransport *t = net_session_->transport();
  std::vector<unsigned char> msg;
  while (t->poll(msg)) {
    Net::Reader r(msg.empty() ? nullptr : &msg[0], msg.size());
    Net::Header h;
    if (!Net::read_header(r, h)) continue;
    if (net_handle_ping_pong(h.msg_type, r)) continue;
    if (h.msg_type == Net::MSG_EVENT) {
      uint8_t code = r.u8();
      uint32_t arg = r.remaining() >= 4 ? r.u32() : 0;
      if (r.ok) net_handle_event(code, arg);
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
      Save::MemStream in(buf);
      Save::GameState s;
      if (!Save::deserialize_game(in, s)) continue;
      if (!net_state_sane(s)) continue;
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

    Save::MemStream in(net_assembler_->payload());
    Save::GameState s;
    if (!Save::deserialize_game(in, s)) continue;
    if (!net_state_sane(s)) continue;  // reject a hostile/corrupt snapshot
    net_apply_state(s);
    net_apply_extras(in, s);
  }
}

void GLGame::net_apply_delta_asteroids(Save::Stream &in) {
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
  if (Net::net_debug_enabled()) {
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
    }
  }

  uint16_t n_rm = 0;
  if (!nx_read(in, n_rm)) return;
  if (n_rm > 5000) return;
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
        Asteroid::play_explode_sound();
        dead_objects->push_back(a);
      } else {
        delete a;
      }
      objects->erase(oi);
      break;
    }
    by_id.erase(f);
  }
}

void GLGame::net_apply_state(const Save::GameState &s) {
  // Generation rollover: the world grew — rebuild boundaries, grid and
  // starfield, drop every stale object (mirrors the host's rollover block;
  // the snapshot then repopulates everything below).
  const bool world_rebuilt = s.world_x != world.x() || s.world_y != world.y();
  net_world_rebuilt_last_apply_ = world_rebuilt;
  if (world_rebuilt) {
    world = Point(s.world_x, s.world_y);
    grid = Grid(world, Point(Asteroid::max_radius * 2, Asteroid::max_radius * 2));
    WrappedPoint::set_boundaries(world);
    delete starfield;
    starfield = new GLStarfield(world, star_density_scale());
    while (!objects->empty()) { delete objects->back(); objects->pop_back(); }
    while (!dead_objects->empty()) { delete dead_objects->back(); dead_objects->pop_back(); }
    Asteroid::num_killable = 0;
  }

  generation = s.generation;
  level_cleared = s.level_cleared;
  time_until_next_generation = s.time_until_next_generation;
  current_time = s.current_time;

  // Players: the remote host ship snaps to the snapshot; the local ship
  // takes stats/weapons but blends its predicted pose toward the host's
  // authoritative one (~0.35 per snapshot) so corrections don't jerk.
  auto it = players->begin();
  for (size_t i = 0; i < s.players.size() && it != players->end(); i++, ++it) {
    Ship *ship = (*it)->ship;
    bool is_local = (*it == players->back());

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
    if (!ship->primary_weapons.empty()) {
      Weapon::Default *dw = dynamic_cast<Weapon::Default *>(*ship->primary);
      if (dw) shot_cooldown = dw->cooldown();
    }
    bool armed_secondary = ship->secondary != ship->secondary_weapons.end() &&
                           (*ship->secondary)->is_shooting();
    float analog_rot = ship->rotation_scale;
    float analog_thrust = ship->thrust_analog;
    float analog_reverse = ship->reverse_analog;

    ship->restore_state(s.players[i], grid);
    // restore_state -> respawn() -> safe_position() relocates the ship to a
    // RANDOM spot whenever the restored position is within 50 units of an
    // asteroid. Right for loading a solo save into a freshly-built world;
    // wrong 10x/s against live snapshots — the host's position is the truth
    // however close to a rock it is. Pin the authoritative position back.
    ship->position = WrappedPoint(s.players[i].pos_x, s.players[i].pos_y);

    // The remote (host) ship is client-extrapolated between applies like
    // every other world object — reconcile instead of overwriting, or it
    // shimmers with the channel's delivery jitter exactly like the
    // asteroids did. Facing/velocity stay authoritative.
    if (!is_local && was_alive && ship->is_alive() && !world_rebuilt)
      net_reconcile_pose(*ship, old_pos, /*sim_exact=*/false);

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

      ship->rotate_left(held_left);
      ship->rotate_right(held_right);
      ship->thrust(held_thrust);
      ship->reverse(held_reverse);
      ship->shoot(armed_shoot);
      ship->fire_secondary(armed_secondary);
      if (shot_cooldown > 0 && !ship->primary_weapons.empty()) {
        Weapon::Default *dw = dynamic_cast<Weapon::Default *>(*ship->primary);
        if (dw) dw->set_cooldown(shot_cooldown);
      }
      ship->rotation_scale = analog_rot;
      ship->thrust_analog = analog_thrust;
      ship->reverse_analog = analog_reverse;
    } else if (is_local && world_rebuilt) {
      // Level transition: keep the authoritative new-level pose and do NOT
      // re-apply the held state — the host cleared the remote ship's
      // controls (and suppresses our held INPUT bits until re-press), so
      // the local prediction starts the level with controls released too,
      // exactly like the host's own player.
      (*it)->net_shoot_held = false;
      (*it)->net_secondary_held = false;
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
    for (const auto &sp : s.pickups) {
      WrappedPoint pos(sp.pos_x, sp.pos_y);
      switch (sp.type) {
        case Save::PickupType::Weapon:   pickups->push_back(new WeaponPickup(pos, sp.weapon_index)); break;
        case Save::PickupType::Mine:     pickups->push_back(new MinePickup(pos)); break;
        case Save::PickupType::GigaMine: pickups->push_back(new GigaMinePickup(pos)); break;
        case Save::PickupType::Missile:  pickups->push_back(new MissilePickup(pos)); break;
        case Save::PickupType::Shield:   pickups->push_back(new ShieldPickup(pos)); break;
        case Save::PickupType::GodMode:  pickups->push_back(new GodModePickup(pos)); break;
        case Save::PickupType::ExtraLife: pickups->push_back(new ExtraLife(pos)); break;
        case Save::PickupType::NovaCharge: pickups->push_back(new NovaChargePickup(pos)); break;
        default: break;
      }
    }
  }

  // Black holes: cheap wholesale rebuild (0 or 1 in practice).
  while (!black_holes->empty()) { delete black_holes->back(); black_holes->pop_back(); }
  for (const auto &sbh : s.black_holes)
    black_holes->push_back(new BlackHole(WrappedPoint(sbh.pos_x, sbh.pos_y)));

  // Stations restore in place; created/destroyed on presence transitions.
  // restore_state APPENDS its deployed enemies (save-load semantics) — at
  // the 10 Hz apply rate that accumulated hundreds of ships and hung the
  // game. All enemies are station-owned: wholesale rebuild each apply.
  if (s.station.present) {
    if (!station)
      station = new GLStation(grid, enemies, players, (std::list<Object *> *)objects);
    while (!enemies->empty()) { delete enemies->back(); enemies->pop_back(); }
    station->restore_state(s.station, grid);
  } else if (station) {
    while (!enemies->empty()) { delete enemies->back(); enemies->pop_back(); }
    delete station;
    station = NULL;
  }
  if (s.mini_station.present && s.mini_station.alive) {
    if (!mini_station)
      mini_station = new GLMiniStation(grid, players, (std::list<Object *> *)objects);
    mini_station->restore_state(s.mini_station);
  } else if (mini_station) {
    delete mini_station;
    mini_station = NULL;
  }
}

void GLGame::net_apply_extras(Save::Stream &in, const Save::GameState &s) {
  if (!net_apply_ship_extras(in, s)) return;
  net_apply_keyframe_asteroid_ids(in, s);
}

bool GLGame::net_apply_ship_extras(Save::Stream &in, const Save::GameState &s) {
  uint32_t nplayers = 0;
  if (!nx_read(in, nplayers)) return false;
  auto it = players->begin();
  for (uint32_t i = 0; i < nplayers; i++) {
    NetShipExtras ex;
    if (!nx_read(in, ex.alive) || !nx_read(in, ex.temperature) ||
        !nx_read(in, ex.time_until_respawn) || !nx_read(in, ex.time_left_invincible) ||
        !nx_read(in, ex.god_ms) || !nx_read(in, ex.shield) ||
        !nx_read(in, ex.warp_count) || !nx_read(in, ex.move_flags))
      return false;
    if (it == players->end()) return false;
    Ship *ship = (*it)->ship;

    // The host moved this ship discontinuously (respawn, teleport, new-level
    // spawn) since the last snapshot: the pose is absolute. For the local
    // ship that overrides net_apply_state's prediction blend, which would
    // otherwise slide the ship across the world to the new position.
    if (*it == players->back() && i < s.players.size()) {
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
      // Host says this ship died: explode locally too.
      ship->kill_stop();
      ship->detonate();
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
    // otherwise plays short full-volume hums that sound random.
    bool local_ship = (i + 1 == nplayers);
    ship->set_shield_hum(local_ship && ship->is_alive() && ship->invincible &&
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
    }
    // god-mode / shield presentation on the client is a Milestone-1 cut
    // (both still function — the host simulates them; only their local
    // visual/audio flourishes are missing).

    if (!nx_read_projectiles(in, *ship, net_world_rebuilt_last_apply_))
      return false;
    ++it;
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
    ms_bullets.push_back(Particle(Point(x, y), Point(vx, vy), 2000.0f));
  }
  if (mini_station) mini_station->bullets.swap(ms_bullets);

  // v7: station enemies' bullets, in the same order the station restore
  // just rebuilt the enemies list.
  uint16_t n_en = 0;
  if (!nx_read(in, n_en)) return false;
  if (n_en > 256) return false;  // hostile/corrupt
  auto ei = enemies->begin();
  for (int e = 0; e < n_en; e++) {
    uint16_t nb = 0;
    if (!nx_read(in, nb)) return false;
    if (nb > 512) return false;
    std::vector<Particle> ebs;
    for (int i = 0; i < nb; i++) {
      float x, y, vx, vy;
      if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) ||
          !nx_read(in, vy))
        return false;
      ebs.push_back(Particle(Point(x, y), Point(vx, vy), 2000.0f));
    }
    if (ei != enemies->end()) {
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
      a->restore_state(sa);
      net_reconcile_pose(*a, old_render, /*sim_exact=*/true);
      by_id.erase(found);
    } else {
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
        Asteroid::play_explode_sound();
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
  if (net_mode_ == NetClient && net_connection_lost_ &&
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

void GLGame::tick(int delta) {
  if (net_mode_ != NetOff) {
    // Name a hole in OUR OWN tick cadence (App Nap on an occluded mac
    // window, a window drag, a debugger): in every other log line it is
    // indistinguishable from a network stall — it produces "input gap"
    // on the peer and correction bursts here.
    if (Net::net_debug_enabled() && delta > 250)
      NET_LOG("net: LOCAL frame stall %d ms\n", delta);
    // Pin the simulation rate online: the =/- time cheats change
    // time_between_steps on ONE machine only, and a 8 vs 7 ms mismatch
    // makes every object drift continuously — permanent rubberbanding
    // that no reconciliation can hide.
    time_between_steps = step_size;
  }
  if (net_mode_ == NetClient) {
    tick_net_client(delta);
    return;
  }
  current_time += delta;

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
    if (test_drop_transport > 0 && net_session_) {
      test_drop_transport -= delta;
      if (test_drop_transport <= 0) {
        test_drop_transport = -1;
        NET_LOG("net: TEST dropping transport\n");
        net_session_->transport()->close();
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

    if (!net_connection_lost_) {
      net_host_poll();
      net_ping_tick(delta);
    }
    if (net_session_ && net_session_->transport()->failed())
      net_connection_lost_ = true;
    if (net_signal_ && !net_connection_lost_) net_host_signal_maintain(delta);
    if (net_connection_lost_) {
      if (net_signal_)
        net_host_rejoin_poll(delta);  // room open: play on, await rejoin
      else
        return;  // frozen; draw shows CONNECTION LOST
    }
    if (net_banner_ms_ > 0) net_banner_ms_ -= delta;
  }

  if (!running) {
    last_tick += delta;
    return;
  }

  time_until_next_step -= delta;

  num_frames++;

  if(Asteroid::num_killable == 0) {
    if(!level_cleared) {
      level_cleared = true;
      time_until_next_generation = 5000;
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
      if(generation == 20) {
        world += Point(3000, 3000);
      } else {
        world += Point(50, 50);
      }
      grid = Grid(world, Point(Asteroid::max_radius*2,Asteroid::max_radius*2));
      if(generation >= 20) {
        if(station != NULL)
          delete station;
        station = new GLStation(grid, enemies, players, (std::list<Object*>*)objects);
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
      // From the level after the black hole appears (generation 9), spawn a
      // small roaming station with a fresh random heading each generation.
      // Created here, after the new world bounds and asteroids are in place, so
      // it gets a valid random starting position inside the new world.
      if(generation >= 10) {
        if(mini_station != NULL)
          delete mini_station;
        mini_station = new GLMiniStation(grid, players, (std::list<Object*>*)objects);
      }
      while(!pickups->empty()) {
        delete pickups->back();
        pickups->pop_back();
      }
      // Reposition the black hole at the new world centre.
      while(!black_holes->empty()) {
        delete black_holes->back();
        black_holes->pop_back();
      }
      if(generation >= 9)
        black_holes->push_back(new BlackHole(WrappedPoint(world.x() / 2.0f, world.y() / 2.0f)));
      std::list<GLShip*>::iterator o;
      for(o = players->begin(); o != players->end(); o++) {
        (*o)->ship->respawn(grid, false);
      }
      level_cleared = false;
      save_progress();
      maybe_start_intro();
      if (net_mode_ == NetHost) {
        net_set_generation_banner(generation);
        net_send_event(Net::EV_GENERATION_START, (uint32_t)generation);
        // Same restriction as the local player: respawn's reset() cleared
        // the remote ship's controls; don't let the still-held INPUT bits
        // re-arm them 8 ms later — each key must be released and re-pressed.
        net_held_suppress_ = 0xffff;
        // The world was rebuilt: deltas would reference dead ids.
        net_force_keyframe_ = true;
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
      mini_station->sound_volume_scale =
          net_mode_ != NetOff
              ? net_listener_volume(mini_station->position)
              : sound_volume_for_point(mini_station->position);
      mini_station->step(step_size, grid);
    }

    // Step black holes (visual animation only).
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      (*bhi)->step(step_size);
    }

    std::list<Asteroid*>::iterator oi;
    for(oi = objects->begin(); oi != objects->end(); oi++) {
      (*oi)->step(step_size);
    }
    for(oi = dead_objects->begin(); oi != dead_objects->end(); oi++) {
      (*oi)->step(step_size);
    }

    // Apply black-hole gravity to asteroids (asteroids pass through, not swallowed).
    // Invincible asteroids are unaffected.
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      oi = objects->begin();
      while(oi != objects->end()) {
        if(!(*oi)->invincible)
          (*bhi)->apply_gravity(**oi, step_size);
        oi++;
      }
    }

    for(o = players->begin(); o != players->end(); o++) {
      (*o)->step(step_size, grid);
    }
    // v12 adopt smoothing: a post-blackout pose catch-up banked by
    // net_host_poll glides in instead of hopping on this screen.
    if (net_mode_ == NetHost && players->size() >= 2)
      net_smooth_step(*players->back()->ship, step_size);

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

    // Apply black-hole gravity to bullets, missiles, and mines.
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
      s->sound_volume_scale = net_mode_ != NetOff
          ? 0.5f * net_listener_volume(s->position)
          : (is_visible_to_any_player(*s) ? 0.5f : 0.0f);
      (*o)->step(step_size, grid);
      // Re-apply boost volume after step since thrust() inside may have reset it
      if(s->boost_sound != NULL) {
        if(s->thrusting || s->reversing) {
          Mix_VolumeChunk(s->boost_sound, (int)(MIX_MAX_VOLUME/8 * s->sound_volume_scale));
        } else if(s->still_rotating_left || s->still_rotating_right) {
          Mix_VolumeChunk(s->boost_sound, (int)(MIX_MAX_VOLUME/16 * s->sound_volume_scale));
        } else {
          Mix_VolumeChunk(s->boost_sound, 0);
        }
      }
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
          float roll = rand() / float(RAND_MAX);
          if(roll < extra_life_drop_chance) {
            pickups->push_back(new ExtraLife((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance) {
            int weapon_index = rand() % 15;
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

    // Update quantum asteroid observation state: collapse when any player looks at
    // it (normal speed), enter superposition otherwise (4x speed so it can sneak
    // up on players who look away). Quantum asteroids are always killable.
    for(oi = objects->begin(); oi != objects->end(); ++oi) {
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
            s->detonate(s->missiles[i].position, s->missiles[i].velocity, 25);
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
            if (Asteroid::thud_sound != NULL)
              Mix_PlayChannel(-1, Asteroid::thud_sound, 0);
            // Station-hull deflection: same thud cue for the net client.
            net_send_event(Net::EV_ROID_THUD, Net::pack_pos(bpos.x(), bpos.y(), world.x(), world.y()));
            station->hit();
            s->bullets[i] = std::move(s->bullets.back());
            s->bullets.pop_back();
            if (!station->is_alive()) break;
          } else {
            ++i;
          }
        }
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
          break;
        }
        for (size_t i = 0; i < s->bullets.size(); ) {
          if (mini_station->Object::collide(s->bullets[i])) {
            s->explode(s->bullets[i].position, mini_station->velocity);
            s->bullets[i] = std::move(s->bullets.back());
            s->bullets.pop_back();
            s->score += GLMiniStation::REWARD;
            mini_station->destroy();
            break;
          } else {
            ++i;
          }
        }
        if (!mini_station->is_alive()) break;
        for (size_t i = 0; i < s->missiles.size(); ) {
          if (mini_station->Object::collide(s->missiles[i])) {
            s->detonate(s->missiles[i].position, s->missiles[i].velocity, 25);
            if (s->missile_explode_sound != NULL)
              Mix_PlayChannel(-1, s->missile_explode_sound, 0);
            s->missiles[i] = std::move(s->missiles.back());
            s->missiles.pop_back();
            s->score += GLMiniStation::REWARD;
            mini_station->destroy();
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
        float vol = net_mode_ != NetOff
                        ? net_listener_volume(mini_station->position)
                        : sound_volume_for_point(mini_station->position);
        if (station_explode_sound != NULL && vol > 0.0f) {
          Mix_VolumeChunk(station_explode_sound, (int)(MIX_MAX_VOLUME * vol));
          Mix_PlayChannel(-1, station_explode_sound, 0);
        }
        net_send_event(Net::EV_STATION_BOOM,
                       Net::pack_pos(mini_station->position.x(),
                                     mini_station->position.y(),
                                     world.x(), world.y()));
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
          if(pickup_sound != NULL)
            Mix_PlayChannel(-1, pickup_sound, 0);
          net_send_event(Net::EV_PICKUP);  // collection cue for the client
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

    time_until_next_step += time_between_steps;
  }
  /* Save high score automatically on game over */
  if (!score_saved && !players->empty()) {
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
      score_saved = true;
      game_over_time = current_time;
#ifdef __EMSCRIPTEN__
      // Show the tap-to-continue overlay so any touch reaches _web_tap_start().
      EM_ASM(if (window.setMenuMode) window.setMenuMode(1););
#endif
    }
  }

  /* Delete save on true game over */
  if (score_saved && !save_deleted_ && net_mode_ == NetOff) {
    Save::delete_save();
    save_deleted_ = true;
  }

  /* Save on death while lives remain (once per death window) */
  if (!score_saved && net_mode_ == NetOff && !players->empty()) {
    bool any_dead_with_lives = false;
    bool any_dead_no_lives   = false;
    for (auto* glship : *players) {
      if (!glship->ship->is_alive()) {
        if (glship->ship->lives > 0) any_dead_with_lives = true;
        else                         any_dead_no_lives   = true;
      }
    }
    if (any_dead_with_lives && !any_dead_no_lives && !save_written_this_death_) {
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
    Ship *remote = players->back()->ship;
    remote->set_shield_hum(false);
    // The sim plays the remote player's own sounds (gun etc.) like a
    // split-screen partner's — full volume. Online the only listener
    // here is the local player: attenuate by distance.
    remote->sound_volume_scale = net_listener_volume(remote->position);
    // While parked for rejoin, re-assert the shield every tick: level
    // rebuilds respawn all players with the normal 1.5 s window, which
    // would otherwise expire and leave the pilotless hull killable.
    if (net_connection_lost_ && net_signal_ && remote->is_alive()) {
      remote->invincible = true;
      remote->time_left_invincible = 1 << 29;
    }
  }

  // (Bullet-vs-asteroid impact cues used to be forwarded here as
  // EV_ROID_THUD/TING events; since PROTO 10 the client detects those
  // cosmetics locally — Ship::net_cosmetic_impacts.)
  if (net_mode_ == NetHost && net_session_ && !net_connection_lost_) {
    // Non-fatal ship-vs-asteroid bounces (debris + armour ting). Enemy
    // ships collide through the same code; only player ships are sent.
    for (const Ship::NetShipImpact &si : Ship::net_ship_impacts) {
      uint32_t idx = 0;
      if (!players->empty() && players->front()->ship == si.ship) idx = 1;
      else if (players->size() >= 2 && players->back()->ship == si.ship) idx = 2;
      if (idx)
        net_send_event(Net::EV_SHIP_IMPACT, idx | (si.ting ? 0x100u : 0u));
    }
    // Shots: the HOST player's go over as EV_REMOTE_SHOT (the client
    // fires its own weapon locally, and the host simulates the client's
    // shots too — neither is relayed). World actors' (enemies, the
    // mini-station) go over with their position for attenuation.
    Ship *p1 = players->empty() ? NULL : players->front()->ship;
    Ship *p2 = players->size() >= 2 ? players->back()->ship : NULL;
    for (const Ship *shooter : Ship::net_shots) {
      if (shooter == p1)
        net_send_event(Net::EV_REMOTE_SHOT, 1);
      else if (shooter != p2)
        net_send_event(Net::EV_WORLD_SHOT,
                       Net::pack_pos(shooter->position.x(),
                                     shooter->position.y(),
                                     world.x(), world.y()));
    }
    // Deaths: player explosions already reach the client through the
    // snapshot extras; world actors' need the event.
    for (const Ship *boom : Ship::net_booms)
      if (boom != p1 && boom != p2)
        net_send_event(Net::EV_WORLD_BOOM,
                       Net::pack_pos(boom->position.x(), boom->position.y(), world.x(), world.y()));
  }
  Ship::net_ship_impacts.clear();
  Ship::net_shots.clear();
  Ship::net_booms.clear();

  // Online host: broadcast the world at 10 Hz once everything has stepped.
  if (net_mode_ == NetHost) net_host_send_snapshot(delta);

  /* Display FPS */
  //std::cout << (num_frames*1000 / current_time) << std::endl;
}

void GLGame::draw_objects(float direction, bool minimap) const {
  if(debug_grid && !minimap) grid.draw_debug();

  for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
    (*bhi)->draw(minimap);
  }

  AsteroidDrawer::draw_batch(objects, dead_objects, direction, minimap,
                             minimap ? world.x() : 0, minimap ? world.y() : 0);

  for(auto pi = pickups->begin(); pi != pickups->end(); pi++) {
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
  Uint32 now = SDL_GetTicks();
  int frame_delta = (int)(now - last_draw_time_);
  last_draw_time_ = now;
  for(GLShip *gs : *players) gs->smooth_camera(frame_delta);

  glClear(GL_COLOR_BUFFER_BIT /*| GL_DEPTH_BUFFER_BIT*/);

  if(players->size() == 0) {
    draw_world();
  }
  else if(net_mode_ != NetOff) {
    // Online: one full-screen view following the local player (host =
    // front of the list, client = back). The peer draws their own view.
    draw_world(local_player(), true);
    draw_map();
    Overlay::net_overlays(this);
  }
  else {
    if(players->size() > 0) {
      draw_world(players->front(), true);
    }
    if(players->size() > 1) {
      draw_world(players->back(), false);
    }
    //Draw map after - for partial translucency
    draw_map();
  }
}


int GLGame::num_x_viewports() const {
  if (net_mode_ != NetOff) return 1;
  return (players->size() == 0) ? 1 : (window.x() > window.y()) ? players->size() : 1;
}

int GLGame::num_y_viewports() const {
  if (net_mode_ != NetOff) return 1;
  return (players->size() == 0) ? 1 : (window.x() > window.y()) ? 1 : players->size();
}

bool GLGame::is_visible_to_any_player(const Ship &ship) const {
  for(auto* glship : *players) {
    if(!glship->ship->is_alive()) continue;
    float fov_deg = glship->view_angle();
    float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
    float aspect = window.x() / (float)(window.y() / num_y_viewports());
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
    float aspect = window.x() / (float)(window.y() / num_y_viewports());
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
float GLGame::net_listener_volume(Point p) const {
  GLShip *me = local_player();
  if (!me || !me->ship->is_alive()) return 0.0f;
  float fov_deg = me->view_angle();
  float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
  float aspect = window.x() / (float)window.y();
  float half_w = half_h * aspect;
  float cull_r = sqrtf(half_w * half_w + half_h * half_h) * sqrtf(1.1f);
  float dist = me->ship->position.distance_to(p);
  if (dist >= cull_r) return 0.0f;
  return 1.0f - dist / cull_r;
}

float GLGame::sound_volume_for_point(Point p) const {
  float best = 0.0f;
  for(auto* glship : *players) {
    if(!glship->ship->is_alive()) continue;
    float fov_deg = glship->view_angle();
    float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
    float aspect = window.x() / (float)(window.y() / num_y_viewports());
    float half_w = half_h * aspect;
    float cull_r = sqrtf(half_w * half_w + half_h * half_h) * sqrtf(1.1f);
    float dist = glship->ship->position.distance_to(p);
    if(dist < cull_r) {
      float v = 1.0f - dist / cull_r;
      if(v > best) best = v;
    }
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
    float aspect = window.x() / (float)(window.y() / num_y_viewports());
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


void GLGame::setup_viewport(bool primary) const {
  if(split_screen() && window.x() <= window.y()) {
    primary = !primary; //HACK: Fix this
  }
  if(primary) {
    glViewport(0, 0, window.x()/num_x_viewports(), window.y()/num_y_viewports());
  } else {
    if(window.x() > window.y()) {
      glViewport(window.x()/num_x_viewports(), 0, window.x()/num_x_viewports(), window.y()/num_y_viewports());
    } else {
      glViewport(0, window.y()/num_y_viewports(), window.x()/num_x_viewports(), window.y()/num_y_viewports());
    }
  }
}

void GLGame::draw_world(GLShip *glship, bool primary) const {
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
  setup_viewport(primary);
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
  setup_viewport(primary);
  float saved_sw = Typer::scaled_window_width;
  Typer::scaled_window_width = capped_hw / Typer::scale * nx;
  Overlay::draw(this, glship);
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
  float aspect = window.x() / (float)(window.y() / num_y_viewports());
  float half_w = half_h * aspect;
  float cull_r2 = (half_w * half_w + half_h * half_h) * 1.1f; // 10% margin for edge objects

  // Read the perspective*lookat VP set by draw_world; tile transforms are layered on top.
  float base_pv[16]; gles2_get_mvp(base_pv);

  // Draw the world tessellated 3x3, culling tiles that are entirely off-screen.
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
  // --- Invisible asteroid lensing: black asteroid polygon + shifted rear stars ---
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
        AsteroidDrawer::draw_invisible_mask(a, ax, ay);
        starfield->draw_stars_near(ax, ay, a->radius);
      }
    }
  }

  // Game objects: drawn directly each tile (no display list) so draw_batch
  // can emit all asteroids in two draw calls per tile instead of one per asteroid.
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
      draw_objects(direction);
    }
  }
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

  // --- Front star lensing (same void + shift, applied after front stars) ---
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
        // No black mask here — draw_batch already renders the invisible asteroid
        // fill behind visible asteroids. Drawing it again after game objects would
        // overdraw visible asteroids on top of invisible ones.
        starfield->draw_front_stars_near(ax, ay, a->radius);
      }
    }
  }

  // --- Warp pass: distort the contents of each invisible asteroid ---
  // Check whether any invisible asteroids are present to avoid the capture cost.
  bool has_invisible = false;
  for (list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
    if ((*it)->invisible && (*it)->alive) { has_invisible = true; break; }
  }

  if (has_invisible) {
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
          warp_pass_->draw(a, a->position.x() + a->net_pose_err.x(),
                           a->position.y() + a->net_pose_err.y(),
                           vp[0], vp[1], vp[2], vp[3]);
        }
      }
    }
    gles2_set_vp(base_pv);
  }

}

void GLGame::draw_map() const {
#if defined(__ANDROID__) || defined(__IOS__)
  return;
#endif
  float minimap_size = num_y_viewports() == 2 ? window.y()/6 : window.y()/4;

  if(split_screen()) {
    /* DRAW CENTER LINE */
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
      if(window.x() < window.y()) {
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
#if defined(__ANDROID__) || defined(__IOS__)
    // Shift the minimap right of the virtual joystick so they don't overlap.
    int map_x = (int)(g_touch_controls.joy_hint_cx + g_touch_controls.joy_radius + Overlay::CORNER_INSET);
#else
    int map_x = (int)Overlay::CORNER_INSET;
#endif
    // The minimap is positioned with a raw pixel viewport, so the safe-area
    // margin must be added here in pixels (the HUD ortho trick can't reach it).
    int safe_px_x = (int)(window.x() * (1.0f - Overlay::SAFE_AREA_SCALE) / 2.0f);
    int safe_px_y = (int)(window.y() * (1.0f - Overlay::SAFE_AREA_SCALE) / 2.0f);
    glViewport(map_x + safe_px_x, (int)Overlay::CORNER_INSET + safe_px_y, minimap_size, minimap_size);
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

void GLGame::controller(SDL_Event event) {
  if (net_connection_lost_ && !(net_mode_ == NetHost && net_signal_)) {
    if (event.type == SDL_CONTROLLERBUTTONDOWN)
      request_state_change(new Menu());
    return;
  }
  if(event.cbutton.type == SDL_CONTROLLERBUTTONDOWN) {
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
          if (!(game_over_time >= 0 && current_time - game_over_time < 3000)) {
            for (auto* glship : *players)
              save_high_score(glship->ship->score);
            request_state_change(new Menu());
          }
        } else {
          toggle_pause();
        }
      } else if(players->size() < 2 && net_mode_ == NetOff) {
        SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event.cbutton.which);
        if(ctrl) add_player2(ctrl);
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
          if (!(game_over_time >= 0 && current_time - game_over_time < 3000)) {
            for (auto* glship : *players)
              save_high_score(glship->ship->score);
            request_state_change(new Menu());
          }
          return;
        }
      } else if(players->size() < 2 && net_mode_ == NetOff) {
        SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event.cbutton.which);
        if(ctrl) add_player2(ctrl);
      }
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
      if(running) toggle_pause();
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      request_state_change(new Menu());
    }
  }

  if(event.type == SDL_CONTROLLERAXISMOTION &&
     event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
     event.caxis.value > 8000) {
    bool all_game_over = !players->empty();
    for (auto* glship : *players) {
      if (glship->ship->is_alive() || glship->ship->lives > 0) {
        all_game_over = false;
        break;
      }
    }
    if (all_game_over) {
      if (game_over_time >= 0 && current_time - game_over_time < 3000)
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

void GLGame::touch_tap(float nx, float ny) {
  if (!is_touch_mode()) return;
  // The bottom strip is the RETURN TO MENU band the overlay labels (the
  // shared TapBand). It exits to the menu from every state that has no
  // other touch exit: GAME OVER, the pause screen, and — online — a
  // local ship that's fully out while the peer plays on.
  if (!TapBand::return_to_menu.contains(nx, ny)) return;
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
    if (game_over_time >= 0 && current_time - game_over_time < 3000) return;
    for (auto *glship : *players)
      save_high_score(glship->ship->score);
    request_state_change(new Menu());
    return;
  }
  GLShip *local = local_player();
  bool local_over = net_mode_ != NetOff && local &&
                    !local->ship->is_alive() && local->ship->lives <= 0;
  if (!running || local_over) {
    save_progress();
    request_state_change(new Menu());
    return;
  }
  // Mid-play, the "friendly fire on/off" HUD text (two-player only) is a
  // toggle region — host only, mirroring the G key.
  if (players->size() >= 2 && net_mode_ != NetClient &&
      ff_toggle_band().contains(nx, ny))
    host_toggle_friendly_fire();
}

GLShip *GLGame::local_player() const {
  if (players->empty()) return NULL;
  return net_mode_ == NetClient ? players->back() : players->front();
}

void GLGame::touch_joystick(float nx, float ny) {
  if(!running || players->empty()) return;
  local_player()->touch_joystick_input(nx, ny);
}

void GLGame::keyboard (unsigned char key, int x, int y) {
  if (!running)
    return;

  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key);
  }
}

void GLGame::keyboard_up (unsigned char key, int x, int y) {
  const GeneralKeys &gk = g_prefs.general_keys;

  if (net_connection_lost_ && !(net_mode_ == NetHost && net_signal_)) {
    request_state_change(new Menu());
    return;
  }

  // Host-only / debug keys are ignored on the online client — it never
  // mutates world state locally (the host's snapshots are authoritative).
  bool host_keys = net_mode_ != NetClient;

  if (host_keys && key == (unsigned char)gk.skip_level) {
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
  }

  if (host_keys && key == (unsigned char)gk.toggle_friendly_fire)
    host_toggle_friendly_fire();
  if (key == (unsigned char)gk.toggle_debug_grid) {
    debug_grid = !debug_grid;
  }
  if (host_keys && key == (unsigned char)gk.time_speed_up && time_between_steps > 1) time_between_steps--;
  if (host_keys && key == (unsigned char)gk.time_slow_down) time_between_steps++;
  if (host_keys && key == (unsigned char)gk.time_reset) time_between_steps = step_size;
  if (key == (unsigned char)gk.pause) toggle_pause();
#if !defined(__ANDROID__) && !defined(__IOS__)
  if (key == (unsigned char)gk.add_player2 && players->size() < 2 && net_mode_ == NetOff) {
    Ship* p1 = players->front()->ship;
    if(p1->is_alive() || p1->lives) {
      GLShip* object = new GLCar(grid, true);
      set_player_keys(object, 1);
      object->ship->set_missile_asteroids((std::list<Object*>*)objects);
      ship_objects->push_back(object->ship);
      for (auto *p : *players) p->ship->set_missile_ships(ship_objects);
      object->ship->set_missile_ships(ship_objects);
      players->push_back(object);
    }
  }
#endif
  // On all platforms: any non-menu key goes to menu when all players are game over,
  // with a short delay so the last shoot input doesn't immediately skip the game over screen.
  if (key != (unsigned char)gk.menu) {
    bool all_game_over = !players->empty();
    for (auto* glship : *players) {
      if (glship->ship->is_alive() || glship->ship->lives > 0) {
        all_game_over = false;
        break;
      }
    }
    if (all_game_over) {
      if (game_over_time >= 0 && current_time - game_over_time < 3000)
        return;
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      request_state_change(new Menu());
      return;
    }
  }
  if (key == (unsigned char)gk.menu) {
    save_progress();
    request_state_change(new Menu());
  }

  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key, false);
  }
}
