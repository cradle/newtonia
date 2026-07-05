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
#include <SDL.h>

#include "gl_compat.h"
#include "mat4.h"
#include "mesh.h"

#include <iostream>
#include <list>
#include <map>

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
}

GLGame::~GLGame() {
  save_progress();
  // Leaving an online game: tell the peer (best effort — a hard close is
  // also detected via the channel-close path).
  if (net_session_ && !net_connection_lost_)
    net_send_event(Net::EV_BYE);
  delete net_session_;  // closes + deletes the transport
  delete net_assembler_;
  if (net_signal_) {    // closes the room; a later rejoin gets no-such-room
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

Save::GameState GLGame::build_save_data() const {
  Save::GameState s;
  s.generation                 = generation;
  s.world_x                    = world.x();
  s.world_y                    = world.y();
  s.level_cleared              = level_cleared;
  s.time_until_next_generation = time_until_next_generation;
  s.current_time               = current_time;

  for (auto* gs : *players)
    s.players.push_back(gs->ship->capture_state());

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
    Point near = object->ship->position.closest_to(p1_ship->position);
    if ((near - p1_ship->position).magnitude() > 400.0f) break;
    object->ship->position = WrappedPoint();
    object->ship->safe_position(grid, false);
  }
  object->ship->bullets.clear();  // drop the lethal spawn-flash debris
}

void GLGame::net_host_poll() {
  NetTransport *t = net_session_->transport();
  Ship *remote = players->size() >= 2 ? players->back()->ship : NULL;

  std::vector<unsigned char> msg;
  while (t->poll(msg)) {
    Net::Reader r(msg.empty() ? nullptr : &msg[0], msg.size());
    Net::Header h;
    if (!Net::read_header(r, h)) continue;
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
                              const std::vector<std::string> &ice_servers) {
  net_signal_ = signal;
  net_room_code_ = room_code;
  net_ice_ = ice_servers;
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
    // First tick after the loss: park the remote ship (frozen respawn
    // timer so it can't bleed lives to drifting asteroids) and re-host.
    if (remote) {
      if (remote->is_alive()) remote->kill_stop();
      remote->time_until_respawn = 1 << 29;
    }
    delete net_session_;
    net_session_ = nullptr;
    net_rehost_ = NetTransport::create();
    net_rehost_offer_sent_ = false;
    if (net_rehost_) {
      net_rehost_->set_ice_servers(net_ice_);
      net_rehost_->start_host();
    }
    printf("net: player 2 lost - room %s reopened for rejoin\n",
           net_room_code_.c_str());
    fflush(stdout);
  }

  if (net_rehost_ && !net_rehost_offer_sent_ &&
      net_rehost_->local_description_ready()) {
    net_signal_->send_offer(net_rehost_->local_description());
    net_rehost_offer_sent_ = true;
  }

  NetSignal::Event ev;
  while (net_signal_->poll(ev)) {
    if (ev.kind == NetSignal::Event::Answer && net_rehost_) {
      net_rehost_->set_remote_answer(ev.text);
      net_session_ = new NetSession(net_rehost_, NetSession::HostRole);
      net_rehost_ = nullptr;
    } else if (ev.kind == NetSignal::Event::Closed) {
      // Signal server gone: no rejoin possible — fall back to the plain
      // CONNECTION LOST freeze.
      net_signal_->close();
      delete net_signal_;
      net_signal_ = nullptr;
      return;
    }
  }

  // Fresh session handshaking (HELLO/WELCOME) over the new transport.
  if (net_session_) {
    net_session_->update(delta);
    if (net_session_->phase() == NetSession::Ready) {
      net_connection_lost_ = false;
      net_have_input_ = false;      // re-baseline the one-shot counters
      net_input_zeroed_ = false;
      net_held_suppress_ = 0xffff;  // fresh presses required, like a spawn
      net_last_input_time_ = current_time;
      if (remote) {
        // Unpark directly — the step-countdown respawn would charge a life.
        remote->respawn(grid, false);
        remote->bullets.clear();  // no lethal spawn-flash debris
      }
      net_set_generation_banner(generation);
      net_banner_text_ = "PLAYER 2 RECONNECTED";
      printf("net: player 2 rejoined\n");
      fflush(stdout);
    } else if (net_session_->phase() == NetSession::Failed ||
               net_session_->phase() == NetSession::Rejected) {
      // Bad handshake (wrong build?): drop it and re-offer for another try.
      delete net_session_;
      net_session_ = nullptr;
      net_rehost_ = NetTransport::create();
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

}  // namespace

void GLGame::net_host_send_snapshot(int delta) {
  if (!net_session_ || net_connection_lost_) return;  // mid-rejoin
  net_snapshot_timer_ += delta;
  if (net_snapshot_timer_ < 100) return;
  net_snapshot_timer_ = 0;

  Save::MemStream payload;
  Save::serialize_game(payload, build_save_data());

  nx_write(payload, (uint32_t)players->size());
  for (auto *gs : *players) nx_write_ship(payload, *gs->ship);

  nx_write(payload, (uint32_t)objects->size());
  for (auto *a : *objects) nx_write(payload, a->net_id);

  Net::send_snapshot(net_session_->transport(), ++net_snapshot_id_,
                     payload.data(), 1);
  // Bandwidth telemetry (M2-6): a line every 10 s of play at 10 Hz.
  net_bytes_sent_ += payload.data().size();
  if (net_snapshot_id_ % 100 == 0) {
    printf("net: snapshot #%u gen=%d asteroids=%d bytes=%d avg10s=%.1f KB/s\n",
           net_snapshot_id_, generation, (int)objects->size(),
           (int)payload.data().size(), net_bytes_sent_ / 10240.0f);
    fflush(stdout);
    net_bytes_sent_ = 0;
  }
}

void GLGame::net_send_event(uint8_t code, uint32_t arg) {
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
      break;
    default:
      break;
  }
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
    snprintf(buf, sizeof(buf), "LEVEL %d - NEW %s", gen + 1, name);
  else
    snprintf(buf, sizeof(buf), "LEVEL %d", gen + 1);
  net_banner_text_ = buf;
  net_banner_ms_ = 2000;
}

// Full-screen text layered over the online game view: the 2 s generation
// banner (replaces the offline Intro state) and the CONNECTION LOST card.
void GLGame::draw_net_overlays() const {
  if (net_banner_ms_ <= 0 && !net_connection_lost_) return;

  glViewport(0, 0, window.x(), window.y());
  float hw = window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  if (net_connection_lost_ && net_mode_ == NetHost && net_signal_) {
    // Rejoinable loss: the game continues — a quiet notice, not a card.
    Typer::draw_centered(0, hh * 0.72f, "PLAYER 2 DISCONNECTED", 16);
    std::string room = "ROOM " + net_room_code_ + " OPEN - THEY CAN REJOIN";
    if ((current_time / 700) % 2 == 0)
      Typer::draw_centered(0, hh * 0.72f - 40, room.c_str(), 12);
  } else if (net_connection_lost_) {
    Typer::draw_centered(0, 60, "CONNECTION LOST", 34);
    if ((current_time / 700) % 2 == 0)
      Typer::draw_centered(0, -80, "PRESS FIRE FOR MENU", 16);
  } else if (net_banner_ms_ > 0) {
    Typer::draw_centered(0, hh * 0.55f, net_banner_text_.c_str(), 22);
  }
}

// ---- client side ---------------------------------------------------------

GLGame::GLGame(const Save::GameState &snapshot, NetSession *session,
               SDL_GameController *controller)
  : GLGame(snapshot, (SDL_GameController *)NULL) {
  net_mode_ = NetClient;
  net_session_ = session;
  net_assembler_ = new Net::SnapshotAssembler();

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
};

bool nx_read_projectiles(Save::Stream &in, Ship &s) {
  uint16_t n = 0;
  float x, y, vx, vy;

  if (!nx_read(in, n)) return false;
  s.bullets.clear();
  for (int i = 0; i < n; i++) {
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    s.bullets.push_back(Particle(Point(x, y), Point(vx, vy), 2000.0f));
  }

  if (!nx_read(in, n)) return false;
  s.mines.clear();
  for (int i = 0; i < n; i++) {
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    s.mines.push_back(Particle(Point(x, y), Point(vx, vy), 60000.0f));
  }

  if (!nx_read(in, n)) return false;
  s.giga_mines.clear();
  for (int i = 0; i < n; i++) {
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy)) return false;
    s.giga_mines.push_back(Particle(Point(x, y), Point(vx, vy), 60000.0f));
  }

  if (!nx_read(in, n)) return false;
  s.missiles.clear();
  for (int i = 0; i < n; i++) {
    float fx, fy, time_left;
    if (!nx_read(in, x) || !nx_read(in, y) || !nx_read(in, vx) || !nx_read(in, vy) ||
        !nx_read(in, fx) || !nx_read(in, fy) || !nx_read(in, time_left)) return false;
    MissileShot m(WrappedPoint(x, y), Point(fx, fy), Point(0, 0));
    m.velocity = Point(vx, vy);
    m.time_left = time_left;
    s.missiles.push_back(m);
  }

  if (!nx_read(in, n)) return false;
  s.shockwaves.clear();
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
  }
  return true;
}

}  // namespace

void GLGame::tick_net_client(int delta) {
  current_time += delta;

  if (!net_connection_lost_) net_client_poll();
  if (net_session_->transport()->failed()) net_connection_lost_ = true;
  if (net_connection_lost_) return;  // frozen; draw shows CONNECTION LOST
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
    for (auto *a : *objects) a->step(step_size);
    for (auto *a : *dead_objects) a->step(step_size);
    for (auto *p : *pickups) p->step(step_size);
    for (auto *gs : *players) gs->step(step_size, grid);
    grid.update((std::list<Object *> *)objects);

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

  std::vector<uint8_t> msg;
  Net::encode_input(msg, in, 2);
  net_session_->transport()->send_unreliable(&msg[0], msg.size());
}

void GLGame::net_client_poll() {
  NetTransport *t = net_session_->transport();
  std::vector<unsigned char> msg;
  while (t->poll(msg)) {
    Net::Reader r(msg.empty() ? nullptr : &msg[0], msg.size());
    Net::Header h;
    if (!Net::read_header(r, h)) continue;
    if (h.msg_type == Net::MSG_EVENT) {
      uint8_t code = r.u8();
      uint32_t arg = r.remaining() >= 4 ? r.u32() : 0;
      if (r.ok) net_handle_event(code, arg);
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

void GLGame::net_apply_state(const Save::GameState &s) {
  // Generation rollover: the world grew — rebuild boundaries, grid and
  // starfield, drop every stale object (mirrors the host's rollover block;
  // the snapshot then repopulates everything below).
  const bool world_rebuilt = s.world_x != world.x() || s.world_y != world.y();
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

    if (is_local && was_alive && ship->is_alive() && !world_rebuilt) {
      // A correction beyond any plausible prediction error means the host
      // moved the ship discontinuously — teleport, respawn, new-level spawn.
      // Snap to the authoritative pose (already set by restore_state,
      // facing included; the old aim is meaningless after a jump) instead
      // of visibly sliding there over several snapshots. The primary signal
      // is the explicit warp count in the extras; this is the backstop.
      const float snap_dist = 250.0f;
      Point snap = ship->position.closest_to(old_pos);
      float cx = snap.x() - old_pos.x();
      float cy = snap.y() - old_pos.y();
      if (cx * cx + cy * cy < snap_dist * snap_dist) {
        ship->position = WrappedPoint(old_pos.x() + cx * 0.35f,
                                      old_pos.y() + cy * 0.35f);
        ship->position.wrap();
        ship->velocity = old_vel + (ship->velocity - old_vel) * 0.35f;
        ship->facing = old_facing;  // aim stays fully local
      } else {
        printf("net: local ship snapped %.0f units (teleport/respawn/new level)\n",
               sqrtf(cx * cx + cy * cy));
      }

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

  // Pickups: rebuilt wholesale each snapshot.
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

  // Black holes: cheap wholesale rebuild (0 or 1 in practice).
  while (!black_holes->empty()) { delete black_holes->back(); black_holes->pop_back(); }
  for (const auto &sbh : s.black_holes)
    black_holes->push_back(new BlackHole(WrappedPoint(sbh.pos_x, sbh.pos_y)));

  // Stations restore in place; created/destroyed on presence transitions.
  if (s.station.present) {
    if (!station)
      station = new GLStation(grid, enemies, players, (std::list<Object *> *)objects);
    station->restore_state(s.station, grid);
  } else if (station) {
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
  uint32_t nplayers = 0;
  if (!nx_read(in, nplayers)) return;
  auto it = players->begin();
  for (uint32_t i = 0; i < nplayers; i++) {
    NetShipExtras ex;
    if (!nx_read(in, ex.alive) || !nx_read(in, ex.temperature) ||
        !nx_read(in, ex.time_until_respawn) || !nx_read(in, ex.time_left_invincible) ||
        !nx_read(in, ex.god_ms) || !nx_read(in, ex.shield) ||
        !nx_read(in, ex.warp_count))
      return;
    if (it == players->end()) return;
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
        printf("net: warp snap (count %u)\n", (unsigned)ex.warp_count);
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
    }
    ship->temperature = ex.temperature;
    ship->time_until_respawn = ex.time_until_respawn;
    ship->time_left_invincible = ex.time_left_invincible;
    // restore_state -> respawn() force-sets invincible=true (and restarts
    // the shield hum) every snapshot; reflect the authoritative state
    // instead or the shield ring flickers and the hum plays constantly.
    ship->invincible = ex.time_left_invincible > 0 || ex.god_ms > 0 ||
                       ex.shield != 0;
    ship->set_shield_hum(ship->is_alive() && ship->invincible &&
                         ex.god_ms <= 0);
    // god-mode / shield presentation on the client is a Milestone-1 cut
    // (both still function — the host simulates them; only their local
    // visual/audio flourishes are missing).

    if (!nx_read_projectiles(in, *ship)) return;
    ++it;
  }

  // Asteroids: match by net_id; new ids construct + restore, missing ids
  // play the standard death visual so host-side kills still explode here.
  uint32_t n_ids = 0;
  if (!nx_read(in, n_ids)) return;
  std::vector<uint32_t> ids(n_ids);
  for (uint32_t i = 0; i < n_ids; i++)
    if (!nx_read(in, ids[i])) return;
  if (n_ids != s.asteroids.size()) return;  // malformed snapshot

  std::map<uint32_t, Asteroid *> by_id;
  for (auto *a : *objects) by_id[a->net_id] = a;

  for (uint32_t i = 0; i < n_ids; i++) {
    const Save::Asteroid &sa = s.asteroids[i];
    std::map<uint32_t, Asteroid *>::iterator found = by_id.find(ids[i]);
    if (found != by_id.end()) {
      found->second->restore_state(sa);
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
      if (!(*oi)->is_removable()) {
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
  if (net_mode_ == NetClient) {
    tick_net_client(delta);
    return;
  }
  current_time += delta;

  // Online host: poll the peer before the pause gate — their RESUME (or a
  // dead transport) must be noticed even while paused.
  if (net_mode_ == NetHost) {
    if (!net_connection_lost_) net_host_poll();
    if (net_session_ && net_session_->transport()->failed())
      net_connection_lost_ = true;
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
      s->sound_volume_scale = is_visible_to_any_player(*s) ? 0.5f : 0.0f;
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
    // For each pair of live asteroids where at least one is elastic, apply
    // 2D elastic collision physics (mass ~ radius^2, conservation of momentum
    // and kinetic energy along the collision normal). Pairs are processed once
    // via inner iterator starting after outer to avoid double-application.
    // Reflective asteroids carry elastic=true so they participate automatically.
    {
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
          if((a->reflective || b->reflective) && Asteroid::asteroid_ting_sound != NULL) {
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
              }
            }
          }
        }
      }
    }

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
      // destroyed it, play the destruction sound once.
      if (!mini_station->is_alive() && station_explode_sound != NULL) {
        Mix_PlayChannel(-1, station_explode_sound, 0);
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
    draw_world(net_mode_ == NetClient ? players->back() : players->front(),
               true);
    draw_map();
    draw_net_overlays();
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

        float ax = a->position.x();
        float ay = a->position.y();
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

        float ax = a->position.x();
        float ay = a->position.y();
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
          warp_pass_->draw(a, a->position.x(), a->position.y(), vp[0], vp[1], vp[2], vp[3]);
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

void GLGame::touch_joystick(float nx, float ny) {
  if(!running || players->empty()) return;
  players->front()->touch_joystick_input(nx, ny);
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

  if (host_keys && key == (unsigned char)gk.toggle_friendly_fire) {
    friendly_fire = !friendly_fire;
    g_prefs.friendly_fire = friendly_fire;
    save_preferences();
  }
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
