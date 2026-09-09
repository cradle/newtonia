// One-hand touch gesture layer: the held deflection (touch_controls.h).
//
// Links touch_controls.cpp against link-time stubs for everything it
// reaches — the three StateManager entry points it drives (recorded as an
// event log), the Preferences global, the zoom-zone lookups and the safe
// inset — and a --wrap'ped SDL_GetTicks, so the clock is the test's and a
// 300 ms window can be stepped past in one line. No SDL runtime, no
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
TouchZone TouchZone::zoom_in_placed()  { return TouchZone(2.0f, 2.0f, 2.0f, 2.0f); }
TouchZone TouchZone::zoom_out_placed() { return TouchZone(2.0f, 2.0f, 2.0f, 2.0f); }

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

// The headline: steer, lift, tap, tap, tap — every tap one shot, the ship
// flying the remembered deflection from the first tap on.
static void test_lift_tap_tap() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);

  // The lift: a stick let go stops the ship NOW, and the memory arms.
  size_t at = s_log.size();
  CHECK(up(1));
  CHECK(any_joy_zero(at));
  CHECK(g_touch_controls.oh_hold_valid && !g_touch_controls.oh_hold_engaged);
  CHECK(near(g_touch_controls.oh_hold_ny, -0.6f));
  frame(16);
  frame(16);
  // Armed, not engaged: nothing drives the ship while it waits.
  CHECK(!any_joy_nonzero(at));

  // Tap 1, 100 ms after the lift, somewhere else on the screen.
  s_now += 68;
  at = s_log.size();
  down(1, 700, 520);
  CHECK(g_touch_controls.oh_hold_engaged);
  CHECK(near(g_touch_controls.joy_nx, nx) && near(g_touch_controls.joy_ny, ny));
  frame(16);  // the entry point's apply carries the nub = the memory
  float jx, jy;
  CHECK(last_joy(at, &jx, &jy) && near(jy, -0.6f));
  s_now += 40;
  CHECK(up(1));
  CHECK(count_key(at, 'd', ' ') == 1);  // one shot
  CHECK(!any_joy_zero(at));             // and no stop
  CHECK(g_touch_controls.oh_hold_engaged && g_touch_controls.oh_hold_until == 0);

  // Coasting between taps: the tick keeps the ship on the memory.
  at = s_log.size();
  frame(16);
  CHECK(last_joy(at, &jx, &jy) && near(jy, -0.6f));
  frame(60);  // the deferred ' ' release lands
  CHECK(count_key(at, 'u', ' ') == 1);

  // Taps 2 and 3, 150 ms apart (fire-hold territory, still one shot each).
  for (int i = 0; i < 2; i++) {
    s_now += 100;
    at = s_log.size();
    down(1, 650 + 30 * i, 540);
    CHECK(g_touch_controls.oh_hold_engaged);
    frame(16);
    s_now += 40;
    CHECK(up(1));
    CHECK(count_key(at, 'd', ' ') == 1);
    CHECK(!any_joy_zero(at));
    frame(16);
    CHECK(last_joy(at, &jx, &jy) && near(jy, -0.6f));
    frame(60);
  }

  // A completed tap resumes the input until the pilot steers again,
  // including long gaps before the next tap.
  at = s_log.size();
  frame(5000);
  CHECK(!any_joy_zero(at));
  CHECK(g_touch_controls.oh_hold_valid && g_touch_controls.oh_hold_engaged);
  CHECK(last_joy(at, &jx, &jy) && near(jy, -0.6f));
  down(1, 600, 400);
  CHECK(g_touch_controls.oh_hold_engaged);
  frame(40);
  CHECK(up(1));
  frame(1000);
  CHECK(!any_joy_zero(at));
  // Take over and return to centre to stop.
  down(1, 600, 400);
  motion(1, 600, 400 - 0.4f*g_touch_controls.joy_radius);
  motion(1, 600, 400);
  frame(16);
  CHECK(up(1));
  CHECK(any_joy_zero(at));
  CHECK(!g_touch_controls.oh_hold_valid);
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
  frame(40);
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

// Re-land inside the window and STEER: the finger's wander takes the
// stick back live from its own landing point, and the lift after that
// remembers only the new thrust.
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
  CHECK(near(g_touch_controls.joy_ny, -0.6f));
  frame(16);
  // A real wander: live deflection from the landing point (600,500).
  float r = g_touch_controls.joy_radius;
  motion(1, 600 + 0.5f * r, 500);
  CHECK(g_touch_controls.oh_joy_steered);
  CHECK(!g_touch_controls.oh_hold_engaged);
  CHECK(near(g_touch_controls.joy_nx, 0.5f) && near(g_touch_controls.joy_ny, 0.0f));
  size_t at = s_log.size();
  frame(16);
  float jx, jy;
  CHECK(last_joy(at, &jx, &jy) && near(jx, 0.5f) && near(jy, 0.0f));
  // A rotation-only drag turns live but cannot leave the ship turning
  // during a later fire tap, even if it lands inside the memory window.
  at = s_log.size();
  CHECK(up(1));
  CHECK(any_joy_zero(at));
  CHECK(count_key(at, 'd', ' ') == 0);  // a steering release never fires
  CHECK(!g_touch_controls.oh_hold_valid);
  frame(100);
  at = s_log.size();
  down(1, 600, 500);
  // Inside the tap threshold, but outside the ship's 0.10 deadzone.
  motion(1, 600 + 0.11f*r, 500);
  frame(16);
  CHECK(up(1));
  frame(1000);
  CHECK(!any_joy_nonzero(at));
  CHECK(count_key(at, 'd', ' ') == 1);
}

// A stick brought back to centre before the lift is a stop, not a
// manoeuvre: nothing to remember.
static void test_centre_release_forgets() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  motion(1, 500, 500);
  frame(16);
  CHECK(up(1));
  CHECK(!g_touch_controls.oh_hold_valid);
}

// The live-play gate dropping (pause, roster, help card, game over) stops
// a coasting ship and forgets the memory.
static void test_gate_off_stops_coast() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  s_now += 100;
  down(1, 600, 500);
  s_now += 40;
  CHECK(up(1));
  CHECK(g_touch_controls.oh_hold_engaged);
  frame(16);
  g_touch_controls.one_hand_ingame = false;
  size_t at = s_log.size();
  frame(16);
  CHECK(any_joy_zero(at));
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

// touch_controls_reset (backgrounding, rotation) stops a coasting ship.
static void test_reset_stops_coast() {
  reset_layer();
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  s_now += 100;
  down(1, 600, 500);
  s_now += 40;
  CHECK(up(1));
  CHECK(g_touch_controls.oh_hold_engaged);
  size_t at = s_log.size();
  touch_controls_reset(SM);
  CHECK(any_joy_zero(at));
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
// its un-wandered release keeps the ship flying.
static void test_long_press_under_memory() {
  reset_layer();
  g_touch_controls.mine_available = true;
  float nx, ny;
  steer_up(1, 500, 500, &nx, &ny);
  CHECK(up(1));
  s_now += 100;
  size_t at = s_log.size();
  down(1, 600, 500);
  CHECK(g_touch_controls.oh_hold_engaged);
  for (int i = 0; i < 30; i++) frame(16);  // 480 ms held still
  CHECK(count_key(at, 'd', 'x') == 1);
  CHECK(count_key(at, 'd', ' ') == 0);
  float jx, jy;
  CHECK(last_joy(at, &jx, &jy) && near(jy, -0.6f));
  at = s_log.size();
  CHECK(up(1));
  CHECK(!any_joy_zero(at));
  CHECK(g_touch_controls.oh_hold_engaged);
}

// A diagonal steer followed by a peeling thumb must not amplify thrust or
// rotation when the pilot taps. Exercise forward and reverse thrust.
static void test_pre_lift_thrust() {
  for (float sign : {-1.0f, 1.0f}) {
    reset_layer();
    down(1, 500, 400);
    float r = g_touch_controls.joy_radius;
    frame(16);
    motion(1, 500 + 0.3f*r, 400 + sign*0.4f*r);
    frame(80);
    motion(1, 500 + 0.6f*r, 400 + sign*0.7f*r);
    frame(10);
    CHECK(up(1));
    CHECK(near(g_touch_controls.oh_hold_nx, 0.0f));
    CHECK(near(g_touch_controls.oh_hold_ny, sign*0.4f));
    frame(100);
    down(1, 600, 400);
    frame(16);
    float x, y;
    CHECK(last_joy(0, &x, &y) && near(x, 0.0f) && near(y, sign*0.4f));
    CHECK(up(1));
    frame(16);
    CHECK(last_joy(0, &x, &y) && near(x, 0.0f) && near(y, sign*0.4f));
  }
}

// Sparse events must still advance the cutoff at release. A deliberate
// change held longer than 50 ms replaces the old thrust, including zero.
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
  motion(1, 600, 400 + 0.5f*g_touch_controls.joy_radius);
  frame(10);  // a short new drag must not reuse the prior press's history
  CHECK(up(1));
  CHECK(near(g_touch_controls.oh_hold_ny, 0.5f));
}

static void test_reversal_does_not_restore_old_direction() {
  reset_layer();
  float x, y;
  steer_up(1, 500, 400, &x, &y);
  frame(80);
  motion(1, 500, 400 + 0.4f*g_touch_controls.joy_radius);
  frame(10);
  CHECK(up(1));
  CHECK(!g_touch_controls.oh_hold_valid);
}

int main() {
  test_pre_lift_thrust();
  test_stationary_sample_and_new_press();
  test_reversal_does_not_restore_old_direction();
  test_lift_tap_tap();
  test_armed_memory_lapses();
  test_reland_and_steer();
  test_centre_release_forgets();
  test_gate_off_stops_coast();
  test_gate_off_zeroes_held_nub();
  test_reset_stops_coast();
  test_second_finger_tap_unchanged();
  test_long_press_under_memory();
  if (s_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", s_failures);
    return 1;
  }
  std::printf("touch_one_hand_test: all checks passed\n");
  return 0;
}
