// Final-score freeze: a pilot's score stops at their LAST death, whatever
// their shots, mines and turrets go on to kill (Ship::freeze_score).
// Run: make NETPLAY=0 test-score-freeze (no display or GL context needed).
#include "ship.h"
#include "grid.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <cassert>
#include <cstdio>

static void ready(Ship &ship, const Grid &grid, int lives, int score) {
  Save::Player p{};
  p.lives = lives;
  p.score = score;
  p.pos_x = p.pos_y = 500;
  p.facing_x = 1;
  p.primary_weapons.push_back({Save::WeaponEntry::Kind::Default, 0, 100});
  p.selected_secondary_idx = 0;
  ship.sound_own_cues = false;
  ship.restore_state(p, grid);
  assert(ship.is_alive() && ship.lives == lives && ship.score == score);
  ship.invincible = false;
  ship.time_left_invincible = 0;
}

// A death with a life to spare freezes nothing: kills made during the
// respawn countdown still count, as they always have.
static void test_spare_life_does_not_freeze(const Grid &grid) {
  Ship ship(grid, true);
  ready(ship, grid, 2, 1000);
  assert(ship.kill());
  assert(!ship.score_frozen());
  ship.score += 250;              // a bullet still in flight lands
  ship.enforce_score_freeze();
  assert(ship.score == 1250);
}

// The last life: everything that resolves after the wreck is discarded.
static void test_last_death_freezes(const Grid &grid) {
  Ship ship(grid, true);
  ready(ship, grid, 0, 4200);
  assert(ship.kill());
  assert(ship.score_frozen());
  ship.score += 500;              // lingering shot / mine / turret kill
  ship.score += 2500;             // e.g. a station bounty
  ship.enforce_score_freeze();
  assert(ship.score == 4200);
  // Steps while fully out keep it frozen at the same figure.
  ship.step(8, grid);
  ship.score += 10;
  ship.enforce_score_freeze();
  assert(ship.score == 4200);
}

// A co-op revive brings the pilot back into the run: scoring resumes from
// the frozen figure, and a later last death freezes afresh.
static void test_revive_resumes_scoring(const Grid &grid) {
  Ship ship(grid, true);
  ready(ship, grid, 0, 300);
  assert(ship.kill());
  // Production order: the step's lingering-round points land, THEN a
  // partner's revive pickup is collected, THEN GLGame enforces. The revive
  // must drop those points itself, or the later enforce is a no-op.
  ship.score += 99;
  ship.revive_one_life();
  assert(ship.score == 300);
  ship.enforce_score_freeze();
  assert(ship.score == 300);
  assert(!ship.score_frozen());
  ship.score += 40;
  ship.enforce_score_freeze();
  assert(ship.score == 340);
}

// A ship emptied without a last-life kill() (lives zeroed while already
// dead) is frozen by the step that first sees it fully out.
static void test_step_backstop(const Grid &grid) {
  Ship ship(grid, true);
  ready(ship, grid, 1, 700);
  assert(ship.kill());
  assert(!ship.score_frozen());
  ship.lives = 0;
  ship.step(8, grid);
  assert(ship.score_frozen());
  ship.score += 100;
  ship.enforce_score_freeze();
  assert(ship.score == 700);
}

extern "C" int __wrap_main() {
  assert(SDL_Init(SDL_INIT_AUDIO) == 0);
  assert(Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 512) == 0);
  WrappedPoint::set_boundaries(Point(2000, 2000));
  Grid grid(Point(2000, 2000), Point(100, 100));
  test_spare_life_does_not_freeze(grid);
  test_last_death_freezes(grid);
  test_revive_resumes_scoring(grid);
  test_step_backstop(grid);
  Mix_CloseAudio();
  SDL_Quit();
  std::puts("score_freeze_test: ok");
  return 0;
}
