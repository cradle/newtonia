// One-hand touch gesture layer: the held deflection (touch_controls.h).
//
// Links touch_controls.cpp against link-time stubs for everything it
// reaches — the three StateManager entry points it drives (recorded as an
// event log), the Preferences global, the zoom-zone lookups and the safe
// inset — and a --wrap'ped SDL_GetTicks, so the clock is the test's and a
// 500 ms window can be stepped past in one line. No SDL runtime, no
// display. Run via test/unit/touch_one_hand.sh.
//
// Every scenario mimics the mobile entry points' loop (android_main.cpp /
// ios_main.mm): events, then the per-tick joystick apply of joy_nx/joy_ny
// while joy_active, then touch_one_hand_tick.
#include "touch_controls.h"
#include "preferences.h"
#include "state_manager.h"
#include "view/overlay.h"
#include "view/tap_band.h"

#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// ---- Stubs ----------------------------------------------------------

Preferences g_prefs;
Preferences::Preferences() {}

static Uint32 s_now = 1000;
extern "C" Uint32 __wrap_SDL_GetTicks(void) { return s_now; }

float Overlay::safe_inset_top() { return 0.0f; }
static bool s_real_zones = false;
TouchZone TouchZone::zoom_in_placed() {
  if (!s_real_zones) return TouchZone(2, 2, 2, 2);
  return g_prefs.touch_handedness == 0 ? TouchZone(0, .4f, .12f, .5f)
                                      : TouchZone(.88f, .4f, 1, .5f);
}
TouchZone TouchZone::zoom_out_placed() {
  if (!s_real_zones) return TouchZone(2, 2, 2, 2);
  return g_prefs.touch_handedness == 0 ? TouchZone(0, .5f, .12f, .6f)
                                      : TouchZone(.88f, .5f, 1, .6f);
}

struct Ev {
  char kind;  // 'j' joystick, 'd' key down, 'u' key up
  float nx, ny;
  unsigned char key;
};
static std::vector<Ev> s_log;

void StateManager::keyboard(unsigned char key, int, int) {
  s_log.push_back(Ev{'d', 0, 0, key});
}
void StateManager::keyboard_up(unsigned char key, int, int) {
  s_log.push_back(Ev{'u', 0, 0, key});
}
void StateManager::touch_joystick(float nx, float ny) {
  s_log.push_back(Ev{'j', nx, ny, 0});
}

// The stubs never touch a member, so storage that was never constructed
// is a fine stand-in for the manager (its ctor lives in state_manager.cpp).
alignas(StateManager) static unsigned char s_sm_storage[sizeof(StateManager)];
static StateManager *SM = reinterpret_cast<StateManager *>(s_sm_storage);

// ---- Harness --------------------------------------------------------

static int s_failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      s_failures++;                                                       \
    }                                                                     \
  } while (0)

static const int W = 1000, H = 600;

static void reset_layer() {
  s_log.clear();
  g_touch_controls = TouchControlsState();
  g_prefs.touch_one_hand = true;
  touch_controls_resize(W, H);
  g_touch_controls.one_hand_ingame = true;
  s_now = 1000;
}

// One entry-point loop iteration `ms` later.
static void frame(Uint32 ms) {
  s_now += ms;
  if (g_touch_controls.joy_active)
    SM->touch_joystick(g_touch_controls.joy_nx, g_touch_controls.joy_ny);
  touch_one_hand_tick(SM);
}

static void down(SDL_FingerID id, float px, float py) {
  touch_one_hand_down(SM, id, px, py, px / W, py / H);
}
static void motion(SDL_FingerID id, float px, float py) {
  touch_one_hand_motion(id, px, py);
}
static bool up(SDL_FingerID id) { return touch_one_hand_up(SM, id); }

// Last joystick apply in the log (nx, ny), or false if none since `from`.
static bool last_joy(size_t from, float *nx, float *ny) {
  for (size_t i = s_log.size(); i-- > from;) {
    if (s_log[i].kind == 'j') {
      *nx = s_log[i].nx;
      *ny = s_log[i].ny;
      return true;
    }
  }
  return false;
}
static int count_key(size_t from, char kind, unsigned char key) {
  int n = 0;
  for (size_t i = from; i < s_log.size(); i++)
    if (s_log[i].kind == kind && s_log[i].key == key) n++;
  return n;
}
static bool any_joy_zero(size_t from) {
  for (size_t i = from; i < s_log.size(); i++)
    if (s_log[i].kind == 'j' && s_log[i].nx == 0.0f && s_log[i].ny == 0.0f)
      return true;
  return false;
}
static bool any_joy_nonzero(size_t from) {
  for (size_t i = from; i < s_log.size(); i++)
    if (s_log[i].kind == 'j' && (s_log[i].nx != 0.0f || s_log[i].ny != 0.0f))
      return true;
  return false;
}
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// A steering press: lands, drags straight up by a good fraction of the
// radius (well past the tap slop), a few frames pass. Leaves the finger
// down. Returns the deflection the stick settled on.
static void steer_up(SDL_FingerID id, float px, float py, float *nx, float *ny) {
  down(id, px, py);
  frame(16);
  float r = g_touch_controls.joy_radius;
  motion(id, px, py - 0.6f * r);
  frame(16);
  frame(16);
  CHECK(g_touch_controls.oh_joy_steered);
  *nx = g_touch_controls.joy_nx;
  *ny = g_touch_controls.joy_ny;
  CHECK(near(*nx, 0.0f) && near(*ny, -0.6f));
}

// ---- Scenarios ------------------------------------------------------

// Every lift releases input; quick taps/reholds resume the saved direction.
static void test_lift_tap_tap() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  CHECK(g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
  const float bx = g_touch_controls.boost_cx, by = g_touch_controls.boost_cy;
  for (int i = 0; i < 3; ++i) {
    frame(i == 0 ? 450 : 100);
    size_t at = s_log.size();
    nx = i == 0 ? -0.4f : 0.4f;
    ny = i == 2 ? 0.6f : -0.6f;
    down(1, 500 + nx*g_touch_controls.joy_radius, 500 + ny*g_touch_controls.joy_radius);
    CHECK(g_touch_controls.oh_hold_engaged);
    CHECK(near(g_touch_controls.joy_cx, 500) && near(g_touch_controls.joy_cy, 500));
    CHECK(near(g_touch_controls.boost_cx, bx) && near(g_touch_controls.boost_cy, by));
    frame(40);
    float jx, jy;
    CHECK(last_joy(at, &jx, &jy) && near(jx, nx) && near(jy, ny));
    at = s_log.size();
    CHECK(up(1));
    CHECK(any_joy_zero(at));
    CHECK(g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
    CHECK(g_touch_controls.oh_hold_until == s_now + 500);
    frame(70);
    CHECK(!any_joy_nonzero(at));
    CHECK(count_key(at, 'u', ' ') == 1);
  }
  size_t at = s_log.size();
  frame(429); // 499 ms since the last release
  CHECK(g_touch_controls.oh_hold_valid);
  CHECK(!any_joy_nonzero(at));
  frame(1);
  CHECK(!g_touch_controls.oh_hold_valid);
  down(1, 600, 400);
  frame(40);
  CHECK(!g_touch_controls.oh_hold_engaged);
  CHECK(!any_joy_nonzero(at));
  CHECK(up(1));
  frame(5000);
  CHECK(!any_joy_nonzero(at));
}

// A lift the pilot never follows up: the memory is forgotten, and the
// next press is the cold press it always was.
static void test_armed_memory_lapses() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  frame(150);
  frame(150);
  frame(200);
  CHECK(!g_touch_controls.oh_hold_valid);
  size_t at = s_log.size();
  down(1, 600, 500);
  CHECK(!g_touch_controls.oh_hold_engaged);
  CHECK(near(g_touch_controls.joy_nx, 0.0f) && near(g_touch_controls.joy_ny, 0.0f));
  frame(16);
  CHECK(!any_joy_nonzero(at));
  s_now += 40;
  CHECK(up(1));
  CHECK(count_key(at, 'd', ' ') == 1);  // a plain tap still fires
}

// Re-land inside the window and steer against the same fixed base.
static void test_reland_and_steer() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  s_now += 100;
  down(1, 600, 500);
  CHECK(g_touch_controls.oh_hold_engaged);
  frame(16);
  // Sub-slop jitter leaves the memory in the nub.
  motion(1, 602, 501);
  CHECK(!g_touch_controls.oh_joy_steered);
  CHECK(near(g_touch_controls.joy_nx, 100/g_touch_controls.joy_radius));
  CHECK(near(g_touch_controls.joy_ny, 0.0f));
  frame(16);
  // A real drag still uses the original base (500,500), clamped at the rim.
  float r = g_touch_controls.joy_radius;
  motion(1, 600 + 0.5f * r, 500);
  CHECK(g_touch_controls.oh_joy_steered);
  CHECK(!g_touch_controls.oh_hold_engaged);
  CHECK(near(g_touch_controls.joy_cx, 500) && near(g_touch_controls.joy_cy, 500));
  CHECK(near(g_touch_controls.joy_nx, 1.0f) && near(g_touch_controls.joy_ny, 0.0f));
  size_t at = s_log.size();
  frame(16);
  float jx, jy;
  CHECK(last_joy(at, &jx, &jy) && near(jx, 1.0f) && near(jy, 0.0f));
  // A horizontal drag also survives the firing tap; tap wobble cannot
  // replace the remembered direction with a small accidental deflection.
  at = s_log.size();
  CHECK(up(1));
  CHECK(any_joy_zero(at));
  CHECK(count_key(at, 'd', ' ') == 0);
  CHECK(g_touch_controls.oh_hold_valid);
  frame(100);
  at = s_log.size();
  down(1, 600, 500);
  motion(1, 600 + 0.11f*r, 500);
  frame(16);
  CHECK(up(1));
  frame(1000);
  CHECK(last_joy(at, &jx, &jy) && near(jx, 0.0f) && near(jy, 0.0f));
  CHECK(!g_touch_controls.oh_hold_valid);
  CHECK(count_key(at, 'd', ' ') == 1);
}

// Neutral release preserves the base too; only expiry permits relocation.
static void test_neutral_base_expires() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  const float bx = g_touch_controls.boost_cx, by = g_touch_controls.boost_cy;
  motion(1, 500, 500);
  frame(16);
  CHECK(up(1));
  CHECK(g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
  CHECK(near(g_touch_controls.oh_hold_nx, 0) && near(g_touch_controls.oh_hold_ny, 0));
  frame(499);
  down(1, 600, 500);
  CHECK(near(g_touch_controls.joy_cx, 500) && near(g_touch_controls.joy_cy, 500));
  motion(1, 550, 450);
  frame(1000); // holding does not let a release timer move the base
  CHECK(near(g_touch_controls.joy_cx, 500) && near(g_touch_controls.joy_cy, 500));
  CHECK(near(g_touch_controls.joy_nx, 50/g_touch_controls.joy_radius));
  CHECK(near(g_touch_controls.joy_ny, -50/g_touch_controls.joy_radius));
  CHECK(near(g_touch_controls.boost_cx, bx) && near(g_touch_controls.boost_cy, by));
  CHECK(up(1));
  frame(500);
  down(1, 600, 300);
  CHECK(near(g_touch_controls.joy_cx, 600) && near(g_touch_controls.joy_cy, 300));
  CHECK(near(g_touch_controls.joy_nx, 0) && near(g_touch_controls.joy_ny, 0));
}

// The live-play gate dropping (pause, roster, help card, game over) stops
// remembered input and forgets the released snapshot.
static void test_gate_off_clears_released_memory() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  s_now += 100;
  down(1, 600, 500);
  s_now += 40;
  CHECK(up(1));
  CHECK(g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
  frame(16);
  g_touch_controls.one_hand_ingame = false;
  size_t at = s_log.size();
  frame(16);
  CHECK(!any_joy_nonzero(at));
  CHECK(!g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
}

// The gate dropping under a STILL finger holding an engaged memory zeroes
// the nub too, so a resume with the finger still down does not fly it.
static void test_gate_off_zeroes_held_nub() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  s_now += 100;
  down(1, 600, 500);
  CHECK(g_touch_controls.oh_hold_engaged);
  g_touch_controls.one_hand_ingame = false;
  frame(16);
  CHECK(near(g_touch_controls.joy_nx, 0.0f) && near(g_touch_controls.joy_ny, 0.0f));
  CHECK(!g_touch_controls.oh_hold_engaged);
  // Its eventual release is an ordinary stop with nothing remembered.
  CHECK(up(1));
  CHECK(!g_touch_controls.oh_hold_valid);
}

// Reset clears the released snapshot before it can be resumed.
static void test_reset_clears_released_memory() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  s_now += 100;
  down(1, 600, 500);
  s_now += 40;
  CHECK(up(1));
  CHECK(g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
  size_t at = s_log.size();
  touch_controls_reset(SM);
  CHECK(!any_joy_nonzero(at));
  CHECK(!g_touch_controls.oh_hold_valid);
}

// A second finger while the first steers is untouched by all this: it
// fires, and the stick under the first finger never moves.
static void test_second_finger_tap_unchanged() {
  reset_layer();
  float nx, ny;
  steer_up(1, 400, 500, &nx, &ny);
  size_t at = s_log.size();
  down(2, 800, 400);
  frame(16);
  s_now += 40;
  CHECK(up(2));
  CHECK(count_key(at, 'd', ' ') == 1);
  CHECK(!any_joy_zero(at));
  CHECK(g_touch_controls.joy_active && near(g_touch_controls.joy_ny, -0.6f));
  CHECK(!g_touch_controls.oh_hold_valid);
  // The steering finger's own lift still remembers.
  CHECK(up(1));
  CHECK(g_touch_controls.oh_hold_valid);
}

// A cold long press that lands inside the window fires the secondary
// (unchanged grammar) with the ship flying the memory throughout, and
// its un-wandered release stops input and starts a fresh memory window.
static void test_long_press_under_memory() {
  reset_layer();
  g_touch_controls.mine_available = true;
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  s_now += 100;
  size_t at = s_log.size();
  down(1, 500, 500 - 0.6f*g_touch_controls.joy_radius);
  CHECK(g_touch_controls.oh_hold_engaged);
  for (int i = 0; i < 30; i++) frame(16);  // 480 ms held still
  CHECK(count_key(at, 'd', 'x') == 1);
  CHECK(count_key(at, 'd', ' ') == 0);
  float jx, jy;
  CHECK(last_joy(at, &jx, &jy) && near(jy, -0.6f));
  at = s_log.size();
  CHECK(up(1));
  CHECK(any_joy_zero(at));
  CHECK(g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
  frame(500);
  CHECK(!g_touch_controls.oh_hold_valid);
}

// Remember both axes of the latest live direction, including top-left.
static void test_last_live_thrust() {
  for (float horizontal : {-0.6f, 0.6f}) for (float sign : {-1.0f, 1.0f}) {
    reset_layer();
    down(1, 500, 400);
    float r = g_touch_controls.joy_radius;
    frame(16);
    motion(1, 500 + 0.3f*r, 400 + sign*0.4f*r);
    frame(80);
    motion(1, 500 + horizontal*r, 400 + sign*0.7f*r);
    frame(10);
    CHECK(up(1));
    CHECK(near(g_touch_controls.oh_hold_nx, horizontal));
    CHECK(near(g_touch_controls.oh_hold_ny, sign*0.7f));
    frame(100);
    down(1, 500 + horizontal*r, 400 + sign*0.7f*r);
    frame(16);
    float x, y;
    CHECK(last_joy(0, &x, &y) && near(x, horizontal) && near(y, sign*0.7f));
    CHECK(up(1));
    frame(16);
    CHECK(last_joy(0, &x, &y) && near(x, 0) && near(y, 0));
    CHECK(g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
  }
}

// Stationary holds and short new drags both remember their latest input.
static void test_stationary_sample_and_new_press() {
  reset_layer();
  float x, y;
  steer_up(1, 500, 500, &x, &y);
  frame(80);
  motion(1, 500, 500 - 0.3f*g_touch_controls.joy_radius);
  frame(60);
  CHECK(up(1));
  CHECK(near(g_touch_controls.oh_hold_ny, -0.3f));
  frame(100);
  down(1, 600, 400);
  motion(1, 500, 500 + 0.5f*g_touch_controls.joy_radius);
  frame(10);  // a short new drag must not reuse the prior press's history
  CHECK(up(1));
  CHECK(near(g_touch_controls.oh_hold_ny, 0.5f));
}

static void test_reversal_remembers_new_direction() {
  reset_layer();
  float x, y;
  steer_up(1, 500, 400, &x, &y);
  frame(80);
  motion(1, 500, 400 + 0.4f*g_touch_controls.joy_radius);
  frame(10);
  CHECK(up(1));
  CHECK(g_touch_controls.oh_hold_valid);
  CHECK(near(g_touch_controls.oh_hold_ny, 0.4f));
  frame(100);
  down(1, 500, 400 + 0.4f*g_touch_controls.joy_radius);
  CHECK(near(g_touch_controls.joy_nx, 0) && near(g_touch_controls.joy_ny, 0.4f));
}

// Draw and hit-test geometry move together at a new base, stay there
// through lift, and freeze while an action button owns another finger.
static void test_active_action_buttons() {
  reset_layer();
  auto &tc = g_touch_controls;
  down(1, 400, 500);
  CHECK(tc.oh_anchor_valid);
  float x = tc.boost_cx, y = tc.boost_cy;
  motion(1, 450, 450);
  CHECK(near(tc.boost_cx, x) && near(tc.boost_cy, y));
  CHECK(up(1));
  CHECK(near(tc.boost_cx, x) && near(tc.boost_cy, y));
  size_t at = s_log.size();
  down(2, x, y);
  CHECK(tc.boost_pressed && count_key(at, 'd', 'e') == 1);
  CHECK(!tc.joy_active);
  frame(1500); // an action finger keeps the base beyond either timeout
  down(3, 100, 200);
  motion(3, 130, 200);
  CHECK(tc.joy_active && tc.joy_finger == 3);
  CHECK(near(tc.boost_cx, x) && near(tc.boost_cy, y));
  CHECK(up(2));
  CHECK(!tc.boost_pressed && count_key(at, 'u', 'e') == 1);
  CHECK(near(tc.boost_cx, x) && near(tc.boost_cy, y));
  CHECK(near(tc.joy_cx, 400) && near(tc.joy_cy, 500));
  CHECK(up(3));
  touch_controls_reset(SM);
  CHECK(!tc.oh_anchor_valid);
  CHECK(count_key(at, 'd', ' ') == 0); // both stick fingers deliberately steered
}

// Sweep every side and the whole viewport, including corners where the
// old resting-ring-only bearing scan could place buttons off-screen.
static void test_action_layout_edges() {
  s_real_zones = true;
  for (const auto &size : {std::pair<int,int>(1000,600), {600,1000}, {600,600}}) {
    int w = size.first, h = size.second;
    for (int side = 0; side < 3; ++side) {
      reset_layer();
      g_prefs.touch_handedness = side;
      touch_controls_resize(w, h);
      auto &tc = g_touch_controls;
      for (int ix = 0; ix <= 10; ++ix) for (int iy = 0; iy <= 10; ++iy) {
        tc.oh_anchor_valid = true;
        tc.joy_cx = w * ix / 10.0f; tc.joy_cy = h * iy / 10.0f;
        touch_controls_relayout();
        float xs[3] = {tc.mine_cx, tc.boost_cx, tc.teleport_cx};
        float ys[3] = {tc.mine_cy, tc.boost_cy, tc.teleport_cy};
        float hit = tc.btn_hit_radius;
        for (int i = 0; i < 3; ++i) {
          CHECK(xs[i] >= hit && xs[i] <= w-hit && ys[i] >= hit && ys[i] <= h-hit);
          CHECK(std::hypot(xs[i]-tc.joy_cx, ys[i]-tc.joy_cy) + .01f >= tc.joy_radius+hit);
          CHECK(std::hypot(xs[i]-tc.pause_cx, ys[i]-tc.pause_cy) + .01f >= tc.pause_hit_radius+hit);
          for (auto z : {TouchZone::zoom_in_placed(), TouchZone::zoom_out_placed()}) {
            float dx = std::max(std::max(z.nx0*w-xs[i], xs[i]-z.nx1*w), 0.0f);
            float dy = std::max(std::max(z.ny0*h-ys[i], ys[i]-z.ny1*h), 0.0f);
            CHECK(std::hypot(dx, dy) + .01f >= hit);
          }
          for (int j = i+1; j < 3; ++j)
            CHECK(std::hypot(xs[i]-xs[j], ys[i]-ys[j]) + .01f >= 2*hit);
        }
      }
    }
  }
  s_real_zones = false;
}

// Intro boundaries leave tap-to-start enabled, so the gameplay gate is
// deliberately true. Both released memory and a stationary resumed nub
// must be forgotten; the next native tick must not undo the ship reset.
static void test_intro_forgets_hold() {
  for (int phase = 0; phase < 3; ++phase) {
    reset_layer();
    float x, y;
    steer_up(1, 500, 400, &x, &y);
    CHECK(up(1));
    frame(100);
    if (phase > 0) {
      down(2, 500, 400 - .6f*g_touch_controls.joy_radius);
      frame(40);
      CHECK(g_touch_controls.oh_hold_engaged);
      CHECK(near(g_touch_controls.joy_ny, -.6f));
      if (phase == 2) CHECK(up(2));
    }
    // phase 0: initial lift window; 1: memory under a finger; 2: renewed window.
    touch_one_hand_clear_hold();
    CHECK(g_touch_controls.one_hand_ingame);
    CHECK(!g_touch_controls.oh_hold_valid);
    CHECK(!g_touch_controls.oh_hold_engaged);
    size_t after_clear = s_log.size();
    frame(16);
    CHECK(!any_joy_nonzero(after_clear));
    if (phase == 1) CHECK(up(2));
    frame(5000);
    CHECK(!any_joy_nonzero(after_clear));
    // A fresh tap still emits fire to dismiss the intro; it must not
    // restore the pre-intro manoeuvre or depend on a closed fire gate.
    size_t fresh = s_log.size();
    down(3, 500, 400);
    frame(40);
    CHECK(up(3));
    frame(16);
    CHECK(count_key(fresh, 'd', ' ') == 1);
    CHECK(!any_joy_nonzero(fresh));
  }
}

// Action use preserves only the base, never unattended movement. The
// return window starts at release, even after a long button hold.
static void test_action_return_window() {
  for (int action = 0; action < 3; ++action) {
    for (int scenario = 0; scenario < 3; ++scenario) {
      reset_layer();
      auto &tc = g_touch_controls;
      tc.mine_available = tc.teleport_ready = true;
      down(1, 400, 500);
      motion(1, 440, 460);
      frame(16);
      CHECK(up(1));
      frame(600); // a slower trip to the action can reclaim the visible base
      float x = action == 0 ? tc.mine_cx : action == 1 ? tc.boost_cx : tc.teleport_cx;
      float y = action == 0 ? tc.mine_cy : action == 1 ? tc.boost_cy : tc.teleport_cy;
      size_t action_start = s_log.size();
      down(2, x, y);
      frame(1500);
      CHECK(tc.oh_hold_valid && !tc.joy_active);
      CHECK(!any_joy_nonzero(action_start));
      if (scenario == 2) touch_one_hand_clear_hold();
      CHECK(up(2));
      frame(scenario == 0 ? 999 : scenario == 1 ? 1000 : 10);
      down(3, 430, 470);
      frame(16);
      if (scenario == 0) {
        CHECK(near(tc.joy_cx, 400) && near(tc.joy_cy, 500));
        CHECK(near(tc.joy_nx, 30/tc.joy_radius));
        CHECK(up(3));
        frame(500); // ordinary steering releases still use the 500 ms window
        down(4, 450, 480);
        CHECK(near(tc.joy_cx, 450) && near(tc.joy_cy, 480));
      } else {
        CHECK(near(tc.joy_cx, 430) && near(tc.joy_cy, 470));
        CHECK(near(tc.joy_nx, 0) && near(tc.joy_ny, 0));
      }
    }
  }
}

static void test_shield_button_toggle() {
  reset_layer();
  TouchControlsState &tc = g_touch_controls;
  tc.mine_available = true;
  tc.secondary_kind = 5; // Save::WeaponEntry::Kind::Shield
  down(1, 400, 500);
  motion(1, 440, 460);
  CHECK(up(1));
  frame(600);
  const float bx = tc.mine_cx, by = tc.mine_cy;
  const size_t begin = s_log.size();
  tc.oh_mine_up_at = s_now + 70; // an earlier secondary's pending pulse
  down(2, bx, by);
  CHECK(count_key(begin, 'd', 'x') == 1);
  CHECK(!tc.joy_active);
  tc.shield_engaged = true; // engine mirror on the next tick
  frame(1500);
  CHECK(up(2));
  CHECK(count_key(begin, 'u', 'x') == 0);
  CHECK(tc.shield_engaged && !tc.mine_pressed);
  frame(999);
  down(3, 430, 470);
  CHECK(near(tc.joy_cx, 400) && near(tc.joy_cy, 500));
  motion(3, 450, 450);
  CHECK(up(3));
  down(4, bx, by);
  CHECK(count_key(begin, 'u', 'x') == 1);
  tc.shield_engaged = false;
  CHECK(up(4));
  CHECK(count_key(begin, 'u', 'x') == 1);

  // Button and long-press gesture read the same engine state.
  frame(1000);
  down(5, 400, 500);
  frame(400);
  CHECK(count_key(begin, 'd', 'x') == 2);
  tc.shield_engaged = true;
  CHECK(up(5));
  down(6, tc.mine_cx, tc.mine_cy);
  tc.shield_engaged = false;
  CHECK(up(6));
  CHECK(count_key(begin, 'u', 'x') == 2);

  // A shield reset by the engine is off; the next tap re-engages it.
  down(7, tc.mine_cx, tc.mine_cy);
  tc.shield_engaged = true;
  CHECK(up(7));
  tc.shield_engaged = false;
  down(8, tc.mine_cx, tc.mine_cy);
  tc.shield_engaged = true;
  CHECK(up(8));
  CHECK(count_key(begin, 'd', 'x') == 4);
  const size_t before_reset = s_log.size();
  touch_controls_reset(SM); // no action finger remains, but Shield is on
  CHECK(count_key(before_reset, 'u', 'x') == 1);
  CHECK(!tc.shield_engaged);
  touch_controls_reset(SM);
  CHECK(count_key(before_reset, 'u', 'x') == 1);

  // Equipment changing under a toggle finger must not change lift semantics.
  down(9, tc.mine_cx, tc.mine_cy);
  tc.secondary_kind = 0;
  const size_t before_up = s_log.size();
  CHECK(up(9));
  CHECK(count_key(before_up, 'u', 'x') == 0);

  reset_layer();
  tc.mine_available = true;
  tc.secondary_kind = 5;
  down(1, tc.mine_cx, tc.mine_cy);
  CHECK(up(1));
  // Background before the next engine tick has mirrored the trigger.
  touch_controls_reset(SM);
  CHECK(count_key(0, 'd', 'x') == 1);
  CHECK(count_key(0, 'u', 'x') == 1);
}

static void test_empty_shield_tap_is_a_press() {
  for (bool gesture : {false, true}) {
    reset_layer();
    TouchControlsState &tc = g_touch_controls;
    tc.mine_available = true;
    tc.secondary_kind = 5;
    tc.shield_engaged = true;
    tc.shield_empty = true;
    if (gesture) {
      down(1, 400, 500);
      frame(400);
    } else down(1, tc.mine_cx, tc.mine_cy);
    CHECK(count_key(0, 'd', 'x') == 1); // intentional disposal, not toggle-off
    CHECK(count_key(0, 'u', 'x') == 0);
    CHECK(up(1));
    CHECK(count_key(0, 'd', 'x') == 1);
    CHECK(count_key(0, 'u', 'x') == 0);
    touch_controls_reset(SM);
    CHECK(count_key(0, 'd', 'x') == 1); // reset cannot ask to dispose
    CHECK(count_key(0, 'u', 'x') == 1);
  }
}

int main() {
  test_empty_shield_tap_is_a_press();
  test_shield_button_toggle();
  test_action_return_window();
  test_intro_forgets_hold();
  test_last_live_thrust();
  test_stationary_sample_and_new_press();
  test_reversal_remembers_new_direction();
  test_lift_tap_tap();
  test_armed_memory_lapses();
  test_reland_and_steer();
  test_neutral_base_expires();
  test_gate_off_clears_released_memory();
  test_gate_off_zeroes_held_nub();
  test_reset_clears_released_memory();
  test_second_finger_tap_unchanged();
  test_long_press_under_memory();
  test_active_action_buttons();
  test_action_layout_edges();
  if (s_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", s_failures);
    return 1;
  }
  std::printf("touch_one_hand_test: all checks passed\n");
  return 0;
}
