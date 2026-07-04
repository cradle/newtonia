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
#include <SDL.h>
#include <list>

using namespace std;

class NetSession;
namespace Net { class SnapshotAssembler; }

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
  void touch_joystick(float nx, float ny);

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
  bool is_visible_to_any_player(const Ship &ship) const;
  bool is_visible_to_any_player(Point p) const;
  float sound_volume_for_point(Point p) const;
  bool is_point_faced_by_any_player(Point p) const;
  bool has_free_controller() const;
private:
  void add_asteroids();
  void add_player2(SDL_GameController *ctrl);
  Save::GameState build_save_data() const;
  void save_progress();   // save only when at least one player is alive or has lives
  void toggle_pause(bool broadcast = true);  // broadcast=false: applying a
                                             // peer's PAUSE/RESUME event
  void draw_map() const;
  void draw_objects(float direction = 0.0f, bool minimap = false) const;
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
  void net_host_send_snapshot(int delta);  // 10 Hz world broadcast

  // Client side: visual/kinematic tick (no kills/drops/generation logic),
  // snapshot consumption, prediction correction and INPUT sending.
  void tick_net_client(int delta);
  void net_client_poll();
  void net_client_send_input();
  void net_apply_state(const Save::GameState &s);
  void net_apply_extras(Save::Stream &in, const Save::GameState &s);

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
  uint8_t net_prev_warp_ = 0;     // client: last seen local-ship warp count
  bool net_have_warp_ = false;    // first snapshot baselines the count
  // Host: held INPUT bits ignored until the client releases the key once.
  // Set to all-ones at each level transition so the remote player starts
  // the new level with controls cleared — exactly like the local player,
  // whose respawn reset() also drops held keys until re-pressed.
  uint16_t net_held_suppress_ = 0;
  Net::SnapshotAssembler *net_assembler_ = nullptr;  // client chunk reassembly

  // Phase 8 polish (see NETPLAY.md)
  void net_send_event(uint8_t code, uint32_t arg = 0);
  void net_handle_event(uint8_t code, uint32_t arg);
  void net_set_generation_banner(int gen);
  void draw_net_overlays() const;   // banner / CONNECTION LOST
  bool net_connection_lost_ = false;
  int net_banner_ms_ = 0;
  std::string net_banner_text_;
  int net_last_input_time_ = 0;     // host: dead-man switch (1 s)
  bool net_input_zeroed_ = false;

  static const int step_size = 8;

  Point world;

  int generation;
  int last_tick, time_until_next_step, num_frames, current_time, time_between_steps;
  Uint32 last_draw_time_;
  int time_until_next_generation;
  bool running, level_cleared, friendly_fire, debug_grid, score_saved;
  bool auto_paused = false;
  bool save_written_this_death_ = false;
  bool save_deleted_ = false;
  int game_over_time;

  static const int default_world_width, default_world_height;
  static const int default_num_asteroids, extra_num_asteroids;
  static const float extra_life_drop_chance;
  static const float weapon_pickup_drop_chance;
  static const float mine_pickup_drop_chance;
  static const float giga_mine_pickup_drop_chance;
  static const float missile_pickup_drop_chance;
  static const float shield_pickup_drop_chance;
  static const float god_mode_pickup_drop_chance;
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
  GLMiniStation *mini_station;
  list<GLShip*> *enemies, *players;
  list<Object*> *ship_objects;  // Ship* (as Object*) for missile homing
};

#endif
