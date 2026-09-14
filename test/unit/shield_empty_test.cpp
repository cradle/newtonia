// Real Ship/Shield inventory regression; GNU ld replaces only the app entry.
// Run: make NETPLAY=0 test-shield-empty (no display or GL context needed).
#include "ship.h"
#include "grid.h"
#include "state_manager.h"
#include "touch_controls.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <cassert>
#include <cstdio>

using Kind = Save::WeaponEntry::Kind;

static void equip(Ship &ship, const Grid &grid, int ammo, int fallback = 0) {
  Save::Player p{};
  p.lives = 3;
  p.pos_x = p.pos_y = 500;
  p.facing_x = 1;
  p.primary_weapons.push_back({Kind::Default, 0, 100});
  if (fallback == 2) p.secondary_weapons.push_back({Kind::Mine, 0, 10});
  p.selected_secondary_idx = (int)p.secondary_weapons.size();
  p.secondary_weapons.push_back({Kind::Shield, 0, ammo});
  if (fallback == 1) p.secondary_weapons.push_back({Kind::Mine, 0, 10});
  ship.sound_own_cues = false;
  ship.restore_state(p, grid);
  ship.invincible = false;
  ship.time_left_invincible = 0;
}

static int selected_ammo(const Ship &ship) {
  const auto p = ship.capture_state();
  return p.secondary_weapons.at(p.selected_secondary_idx).ammo;
}

// Keep the production key latch and touch handler; replace only the screen
// forwarding so this test needs no window, GL renderer or running level.
class ShieldInputState : public State {
  Ship &ship;
public:
  explicit ShieldInputState(Ship &s) : ship(s) {}
  void draw() override {}
  void keyboard(unsigned char key, int, int) override {
    if (key == 'x') ship.fire_secondary(true);
  }
  void keyboard_up(unsigned char key, int, int) override {
    if (key == 'x') ship.fire_secondary(false);
  }
  void controller(SDL_Event) override {}
  void tick(int) override {}
};

static void test_touch_disposal_through_key_latch(const Grid &grid) {
  for (int fallback : {0, 1}) {
    Ship ship(grid, true);
    equip(ship, grid, 1, fallback);
    StateManager manager(new ShieldInputState(ship));
    g_prefs.touch_one_hand = true;
    g_touch_controls = TouchControlsState();
    touch_controls_resize(1000, 600);
    auto &tc = g_touch_controls;
    tc.one_hand_ingame = tc.mine_available = true;
    tc.secondary_kind = (unsigned char)Kind::Shield;
    auto tap = [&](SDL_FingerID id) {
      touch_one_hand_down(&manager, id, tc.mine_cx, tc.mine_cy,
                          tc.mine_cx / 1000, tc.mine_cy / 600);
      assert(touch_one_hand_up(&manager, id));
    };
    tap(1); // toggling on leaves StateManager's x key held
    assert(selected_ammo(ship) == 0);
    tc.shield_engaged = tc.shield_empty = true; // next game tick's mirror
    tap(2); // must get past the real duplicate-keydown filter
    auto p = ship.capture_state();
    assert(p.secondary_weapons.size() == (fallback ? 1u : 0u));
    assert(ship.shield_active());
    if (fallback) {
      assert(p.secondary_weapons[0].ammo == 10 && ship.mines.empty());
      tc.secondary_kind = (unsigned char)Kind::Mine;
      tc.shield_engaged = tc.shield_empty = false;
      tap(3); // disposal must also leave the key ready for the next weapon
      assert(selected_ammo(ship) == 9 && ship.mines.size() == 1);
    }
  }
}

extern "C" int __wrap_main() {
  assert(SDL_Init(SDL_INIT_AUDIO) == 0);
  assert(Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 512) == 0);
  WrappedPoint::set_boundaries(Point(2000, 2000));
  Grid grid(Point(2000, 2000), Point(100, 100));
  test_touch_disposal_through_key_latch(grid);

  // A deliberate tap on an empty Shield sends a press, even if toggled on.
  // Check the final charge still protecting us and protection expired,
  // with no fallback, a next weapon, and wrapping to an earlier weapon.
  for (int fallback = 0; fallback < 3; ++fallback) {
    for (bool protection_expired : {false, true}) {
      Ship ship(grid, true);
      equip(ship, grid, 2, fallback);
      ship.fire_secondary(true);
      assert(selected_ammo(ship) == 1);
      for (int i = 0; i < 130 && selected_ammo(ship) > 0; ++i)
        ship.step(8, grid); // held trigger renews after the first charge
      assert(selected_ammo(ship) == 0 && ship.invincible);
      if (protection_expired)
        for (int i = 0; i < 130; ++i) ship.step(8, grid);
      const int duration = ship.time_left_invincible;
      assert(ship.invincible == !protection_expired);
      assert(ship.shield_active() == !protection_expired);
      ship.fire_secondary(false); // pause, intro and ordinary releases do not dispose
      assert(ship.capture_state().secondary_weapons.size() == (fallback ? 2u : 1u));
      ship.fire_secondary(true); // one deliberate tap discards it
      const auto p = ship.capture_state();
      assert(p.secondary_weapons.size() == (fallback ? 1u : 0u));
      assert(ship.invincible == !protection_expired);
      assert(ship.shield_active() == !protection_expired);
      assert(ship.time_left_invincible == duration);
      if (fallback) {
        assert(p.selected_secondary_idx == 0);
        assert(p.secondary_weapons[0].kind == Kind::Mine);
        assert(p.secondary_weapons[0].ammo == 10 && ship.mines.empty());
      } else assert(p.selected_secondary_idx == -1);
      // nx snapshot extras carry shield_active independently of inventory.
      Ship peer(grid, true);
      peer.sound_own_cues = false;
      peer.restore_state(p, grid);
      assert(!peer.shield_effect_active); // reset must forget a former charge
      peer.time_left_invincible = duration;
      peer.invincible = ship.invincible;
      peer.shield_effect_active = ship.shield_active();
      assert(peer.shield_active() == !protection_expired);
      ship.fire_secondary(false); // late release is harmless
      assert(ship.capture_state().secondary_weapons.size() == (fallback ? 1u : 0u));
      for (int i = 0; i < 130; ++i) ship.step(8, grid);
      assert(!ship.shield_active());
      for (int i = 0; i < 130; ++i) peer.step(8, grid);
      assert(!peer.shield_active());
      peer.shield_effect_active = true;
      peer.restore_state(p, grid);
      assert(!peer.shield_effect_active && !peer.shield_active());
    }
  }
  {
    // The host must consume a client's deliberate disposal press even
    // though the exhausted Shield still reports a held trigger.
    Ship ship(grid, true);
    equip(ship, grid, 1, 1);
    ship.fire_secondary(true);
    assert(selected_ammo(ship) == 0 && ship.shield_active());
    ship.net_queued_secondary_presses = 1;
    ship.step(8, grid);
    const auto p = ship.capture_state();
    assert(p.secondary_weapons.size() == 1);
    assert(p.secondary_weapons[0].kind == Kind::Mine);
    assert(p.secondary_weapons[0].ammo == 10 && ship.mines.empty());
    assert(ship.net_queued_secondary_presses == 0 && ship.shield_active());
  }
  {
    Ship ship(grid, true);
    equip(ship, grid, 2);
    ship.fire_secondary(true);
    ship.fire_secondary(false);
    for (int i = 0; i < 130; ++i) ship.step(8, grid);
    assert(selected_ammo(ship) == 1); // nonempty toggle stops renewal
    // An empty, already-off Shield is still discarded on a press.
    equip(ship, grid, 0);
    ship.fire_secondary(true);
    assert(ship.capture_state().secondary_weapons.empty());
  }
  {
    Ship ship(grid, true);
    equip(ship, grid, 0);
    ship.fire_secondary(true);
    ship.add_mine_ammo(0);
    ship.fire_secondary(false);
    assert(ship.capture_state().secondary_weapons.size() == 1);
    ship.fire_secondary(true);
    assert(ship.capture_state().secondary_weapons.empty());
  }
  Mix_CloseAudio();
  SDL_Quit();
  std::puts("shield_empty_test: all checks passed");
  return 0;
}
