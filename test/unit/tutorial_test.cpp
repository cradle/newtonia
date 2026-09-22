// Integration regressions against the real game and tutorial. The runner
// uses -fno-access-control to inspect state without adding production APIs.
#include "glgame.h"
#include "tutorial.h"
#include "glship.h"
#include "asteroid.h"
#include "preferences.h"
#include "touch_controls.h"
#include "typer.h"
#include "gl_compat.h"
#include "achievements.h"
#include "stats.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iterator>

static void press(GLGame &g, unsigned char key) {
  g.keyboard(key, 0, 0);
  g.keyboard_up(key, 0, 0);
}

static void tap_row(GLGame &g, int index, int count) {
  TapBand row = Tutorial::prompt_row(index, count);
  // A real native/web tap: key-down, position, synthesized key-up.
  g.keyboard('\r', 0, 0);
  g.touch_tap(0.5f, 0.5f - (row.y - row.size) / (2 * Typer::scaled_window_height));
  g.keyboard_up('\r', 0, 0);
}

static std::string contents(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "touch" || scenario == "touch-one")
    SDL_setenv("NEWTONIA_FORCE_TOUCH", "1", 1);
  assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0);
  assert(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0);
  SDL_Window *window = SDL_CreateWindow("Tutorial regression", 0, 0, 800, 600,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!window) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
  assert(SDL_GL_CreateContext(window));
  gles2_init();
  Typer::resize(800, 600);
  g_prefs.tutorial_done = false;
  GLGame *g = GLGame::start_tutorial(PAD_NONE);
  g->resize(800, 600);
  Tutorial *t = g->tutorial_;
  Ship *ship = g->players->front()->ship;
  if (scenario == "handoff") {
    t->enter_step(*g, Tutorial::FIRE);
    assert(Asteroid::num_killable == 3);
    t->finish(*g);
    GLGame *next = dynamic_cast<GLGame *>(g->get_next_state());
    assert(next && !next->in_tutorial());
    const int expected = (int)next->objects->size();
    delete g;
    assert(Asteroid::num_killable == expected);
    next->tick(16);
    assert(!next->level_cleared && next->generation == 0);
    // Avoid writing a save from the fixture's real-game successor.
    next->save_dirty_ = false;
    delete next;
  } else if (scenario == "touch" || scenario == "touch-one") {
    assert(t->step() == Tutorial::INPUT);
    bool one = scenario == "touch-one";
    tap_row(*g, one ? 0 : 1, 2);
    assert(t->step() == Tutorial::HAND);
    assert(g_prefs.touch_one_hand == one);
    assert(!g->touch_zoom_active());
    g->keyboard('\r', 0, 0);
    g->touch_tap(0.5f, 0.02f); // outside the card must not confirm
    g->keyboard_up('\r', 0, 0);
    assert(t->step() == Tutorial::HAND);
    tap_row(*g, 0, one ? 3 : 2);
    assert(t->step() == Tutorial::LAUNCH && g_prefs.touch_handedness == 0);
    ship->respawn(g->grid, false);
    t->tick(*g, 16);
    assert(t->step() == Tutorial::TURN);
    t->enter_step(*g, Tutorial::THRUST);
    ship->position = t->beacon_;
    t->tick(*g, 16);
    assert(t->step() == Tutorial::FIRE && !t->beacon_on_);
    assert(g->touch_zoom_active());
    delete g;
  } else if (scenario == "pause") {
    t->enter_step(*g, Tutorial::CAMERA);
    g->focus_lost();
    assert(!g->running);
    g->keyboard_up('p', 0, 0);
    assert(g->running);
    assert(t->step() == Tutorial::CAMERA && t->owns_input());
    // A held gameplay fire released into a new card cannot answer it.
    g->keyboard_up(' ', 0, 0);
    assert(t->step() == Tutorial::CAMERA);
    press(*g, 's'); press(*g, '\r');
    assert(t->step() == Tutorial::TURN && !g_prefs.player_keys[0].rotate_view);
    t->enter_step(*g, Tutorial::CAMERA);
    press(*g, '\r');
    assert(t->step() == Tutorial::FIRE);
    delete g;
  } else if (scenario == "coasting") {
    ship->respawn(g->grid, false);
    ship->velocity = Point(0, 0.4f);
    ship->facing = Point(0, 1);
    t->enter_step(*g, Tutorial::FIRE);
    for (Asteroid *rock : *g->objects) {
      Point offset = rock->position.closest_to(ship->position) - ship->position;
      assert(std::fabs(offset.x()) > ship->radius + rock->radius + 120.0f);
      assert(rock->velocity.magnitude() == 0);
    }
    for (int ms = 0; ms < 3000; ms += 16) g->tick(16);
    assert(ship->is_alive() && t->step() == Tutorial::FIRE);
    delete g;
  } else if (scenario == "actions") {
    ship->respawn(g->grid, false);
    ship->boost(); // a boost from the previous lesson is not completion
    t->enter_step(*g, Tutorial::BOOST);
    t->tick(*g, 16);
    assert(t->step() == Tutorial::BOOST);
    ship->boost_cooldown_left = 0;
    ship->boost();
    ship->weapons_fired_mask = 1u << (int)Save::WeaponEntry::Kind::Missile;
    t->tick(*g, 16);
    assert(t->step() == Tutorial::SECONDARY);
    t->tick(*g, 16);
    assert(t->step() == Tutorial::SECONDARY);
    // Collect the actual tutorial crate and fire its weapon through the sim.
    g->pickups->front()->apply(ship);
    press(*g, 'x');
    for (int ms = 0; ms < 400; ms += 16) g->tick(16);
    assert(t->step() == Tutorial::WRAP && g_prefs.tutorial_done);
    press(*g, ' '); t->tick(*g, 16);
    assert(t->step() == Tutorial::DONE);
    delete g;
  } else if (scenario == "persistence") {
    char *dir = SDL_GetPrefPath("cc.gfm", "newtonia");
    const char *files[] = {"savegame.dat", "stats.dat", "highscore.dat"};
    for (const char *file : files) std::ofstream(std::string(dir) + file) << "existing player data";
    ship->respawn(g->grid, false);
    for (int ms = 0; ms < 61000; ms += 16) g->tick(16);
    g->save_progress();
    assert(Achievements::unlocks_suppressed());
    assert(!g->replay_);
    delete g;
    for (const char *file : files)
      assert(contents(std::string(dir) + file) == "existing player data");
    SDL_free(dir);
  } else if (scenario == "controller") {
    int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                          SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    assert(device >= 0);
    SDL_GameController *pad = SDL_GameControllerOpen(device);
    assert(pad);
    PadId id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
    g->players->front()->set_controller(id);
    t->enter_step(*g, Tutorial::CAMERA);
    SDL_Event event = {};
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.which = id + 100; // unseated pads cannot choose for P1
    event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    g->controller(event);
    assert(t->step() == Tutorial::CAMERA);
    g->toggle_pause();
    event.cbutton.which = id;
    event.cbutton.button = SDL_CONTROLLER_BUTTON_START;
    g->controller(event);
    assert(g->running && t->step() == Tutorial::CAMERA);
    event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    g->controller(event);
    assert(t->step() == Tutorial::FIRE);
    delete g;
  } else {
    assert(false && "unknown scenario");
  }
  printf("tutorial_test: %s passed\n", scenario.c_str());
  return 0;
}
