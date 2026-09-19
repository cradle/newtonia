// Fixtures stub game state and player lifetime, not the analytics decisions.
// KeyBinding is the real production type. The runner inserts production methods.
#include "preferences.h"
#include <cassert>
#include <cstdio>
#include <vector>

Preferences::Preferences() {} // No per-seat defaults or persistence needed here.
Preferences g_prefs;
struct Ship { bool alive = true; bool is_alive() const { return alive; } };
struct GLShip {
  Ship body;
  Ship *ship = &body;
  bool keyed = true;
  bool has_keys() const { return keyed; }
  KeyBinding thrust_key, reverse_key, left_key, right_key, shoot_key, mine_key,
      boost_key, teleport_key, next_weapon_key, next_secondary_key, zoom_in_key,
      zoom_out_key, toggle_rotate_view_key;
  int control_analytics(unsigned char key) const;
};
struct State { virtual ~State() {} };
enum { NetOff, NetHost, NetClient, NetReplay };
struct GLGame : State {
  bool running = true, game_over = false, touch_help_active_ = false;
  bool board = false, card = false, roster = false, spectator = false, arming = false;
  int net_mode_ = NetOff;
  std::vector<GLShip*> seats;
  std::vector<GLShip*> *players = &seats;
  const GLShip *local_player() const { return seats.empty() ? nullptr : seats.front(); }
  bool board_prompt_active() const { return board; }
  bool net_card_owns_input() const { return card; }
  bool roster_open() const { return roster; }
  bool is_spectating() const { return spectator; }
  bool spectate_arming() const { return arming; }
  int control_analytics(int key) const;
};
struct StateManager {
  State *state = nullptr;
  int control_analytics(int key) const;
  bool debug_skip_corner_tap(float, float) { return false; }
  void keyboard(unsigned char, int, int) {}
  void keyboard_up(unsigned char key, int, int) {
    if (key == 'p') {
      GLGame *game = dynamic_cast<GLGame*>(state);
      if (game) game->running = !game->running;
    }
  }
  void touch_tap(float, float) {}
};

// PRODUCTION_METHODS

using SDL_FingerID = long long;
struct FingerKey { SDL_FingerID finger_id; unsigned char key; };
static const int MAX_FINGERS = 10;
static FingerKey s_finger_keys[MAX_FINGERS];
static int s_finger_count = 0;
static bool s_pause_active = false;
static SDL_FingerID s_pause_finger = 0;
static StateManager *s_game = nullptr;
static struct { bool mine_available = true; } g_touch_controls;
static bool touch_layout_mirrored() { return false; }
static std::vector<int> recorded;
static void web_record_touch_control(int mask) {
  if (mask > 0) recorded.push_back(mask);
}

// PRODUCTION_FINGERS

static void finger_motion(SDL_FingerID id, float x, float y) {
  struct { struct { SDL_FingerID fingerId; float x, y; } tfinger; } e = {{id, x, y}};
  do {
    // PRODUCTION_MOTION
  } while (false);
}

int main() {
  GLShip p1, p2;
  GLGame game;
  game.seats = {&p1, &p2};
  p1.thrust_key = KeyBinding('w', 229);
  p2.thrust_key = 'i';
  p1.shoot_key = ' ';
  p1.mine_key = 'x';
  p1.boost_key = 'e';
  p1.teleport_key = 't';
  p1.next_weapon_key = 'q';
  p1.next_secondary_key = 'c';
  p1.zoom_in_key = '2';
  p1.zoom_out_key = '1';
  p1.toggle_rotate_view_key = 'v';
  assert(game.control_analytics(0) == 0);
  assert(game.control_analytics('w') == 1);
  assert(game.control_analytics(229) == 1);
  assert(game.control_analytics(' ') == 2);
  assert(game.control_analytics('x') == 4);
  assert(game.control_analytics('e') == 8);
  assert(game.control_analytics('t') == 16);
  for (int key : {'q', 'c'}) assert(game.control_analytics(key) == 64);
  for (int key : {'1', '2', 'v'}) assert(game.control_analytics(key) == 128);
  p1.boost_key = KeyBinding('z', 'b');
  assert(game.control_analytics('e') == 0);
  assert(game.control_analytics('b') == 8);
  p1.shoot_key = 'z';
  assert(game.control_analytics('z') == (2 | 8));
  p1.body.alive = false;
  assert(game.control_analytics('z') == 0);
  assert(game.control_analytics('i') == 1);
  assert(game.control_analytics('p') == 32);
  p2.keyed = false;
  assert(game.control_analytics('i') == 0);
  p2.keyed = true;
  game.running = false;
  assert(game.control_analytics(0) == -1);
  assert(game.control_analytics('i') == -1);
  assert(game.control_analytics('p') == 32);
  assert(game.control_analytics(27) == -1);
  for (int mode : {NetHost, NetClient}) {
    game.net_mode_ = mode;
    assert(game.control_analytics(27) == 32);
    assert(game.control_analytics('p') == 32);
    game.running = true;
    assert(game.control_analytics(27) == 32);
    game.running = false;
  }
  // Exclusions dominate both the running and paused pause-key exceptions.
  for (bool running : {false, true}) {
    game.running = running;
    for (bool *flag : {&game.game_over, &game.board, &game.card, &game.roster,
                      &game.touch_help_active_, &game.spectator, &game.arming}) {
      *flag = true;
      for (int key : {0, int('i'), int('p'), 27})
        assert(game.control_analytics(key) == -1);
      *flag = false;
    }
    game.net_mode_ = NetReplay;
    assert(game.control_analytics('p') == -1);
    assert(game.control_analytics(27) == -1);
    game.net_mode_ = NetHost;
  }
  StateManager manager;
  State menu;
  assert(manager.control_analytics('p') == -1);
  manager.state = &menu;
  assert(manager.control_analytics('p') == -1);
  manager.state = &game;
  assert(manager.control_analytics('i') == 1);
  s_game = &manager;
  p1.body.alive = true;
  p1.left_key = 'a'; p1.right_key = 'd';
  finger_down(1, 0.1f, 0.1f);
  finger_motion(1, 0.1f, 0.5f);
  finger_motion(1, 0.4f, 0.5f);
  assert(recorded == std::vector<int>({1}));
  finger_up(1, 0.4f, 0.5f);
  finger_down(2, 0.1f, 0.1f);
  assert(recorded == std::vector<int>({1, 1}));
  finger_up(2, 0.1f, 0.1f);
  for (float x : {0.5f, 0.9f}) {
    float y = x == 0.5f ? 0.4f : 0.1f;
    recorded.clear();
    finger_down(3, x, y); finger_up(3, x, y);
    assert(!game.running);
    finger_down(4, x, y); finger_up(4, x, y);
    assert(game.running);
    assert(recorded == std::vector<int>({32, 32}));
    game.roster = true;
    recorded.clear();
    finger_down(5, x, y); finger_up(5, x, y);
    assert(recorded.empty());
    game.roster = false;
    game.running = true;
  }
  game.seats.clear();
  assert(manager.control_analytics('p') == -1);
  std::puts("C++ control analytics: gates, seats, remaps and alternates passed");
}
