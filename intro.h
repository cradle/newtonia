#ifndef INTRO_H
#define INTRO_H

#include "state.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <vector>

class GLGame;
class GLEnemy;
class Asteroid;
class Hazard;

// Between-level intro screen shown when a generation introduces a new object
// type: the game world freezes and the object spins centre-screen with its
// name and a flashing "PRESS FIRE TO START"; any player's shoot input
// dismisses it, or it auto-starts after a timeout. The intro adopts the
// GLGame that spawned it (the game is not deleted on the transition) and
// hands it back to the StateManager on dismissal, so play resumes exactly
// where it froze. Not persisted in saves — resuming a save never re-shows
// one.
class Intro : public State {
public:
  enum Kind { ASTEROID, BLACK_HOLE, MINI_STATION, STATION, HAZARD, INTERCEPTOR, BOMBER, RAMMER };

  // Takes ownership of `game` (handed back to the StateManager on dismissal,
  // or deleted — which auto-saves — when leaving to the menu) and of every
  // asteroid in `display_asteroids` (Kind ASTEROID only, empty otherwise;
  // display-only, they never enter the game's object lists or collision
  // grid). One rock spins centre-screen; several — the TOUGH ELITES intro —
  // are laid out in a row around the focus. `hazard_kind` selects which live
  // hazard to focus on for Kind HAZARD (a Hazard::Kind value); ignored
  // otherwise.
  Intro(GLGame *game, Kind kind, const char *name,
        std::vector<Asteroid *> display_asteroids, int hazard_kind = -1);
  virtual ~Intro();

  void draw() override;
  void tick(int delta) override;
  void keyboard(unsigned char key, int x, int y) override;
  void keyboard_up(unsigned char key, int x, int y) override;
  void controller(SDL_Event event) override;
  void touch_tap(float nx, float ny) override;
  bool back_pressed() override;

  // Called by StateManager (which cannot go through State virtuals for
  // these); controller hot-plug is forwarded to the adopted game so
  // assignments survive the intro.
  void focus_lost();
  void focus_gained();
  void controller_added(PadId id);
  void controller_removed(PadId id);
  // The intro's only inputs are fire (dismiss), Start (pause) and back —
  // the Ship set, so a pilot's fire binding starts the level.
  PadActionSet pad_action_set() const override { return PAD_SET_SHIP; }

private:
  void dismiss();        // hand the game back to the state manager
  void leave_to_menu();  // abandon the game (deleted in ~Intro, which saves)
  void toggle_pause();   // freeze the auto-start countdown / intro tune
  Point focus() const;

  // Ticked ms the camera smoothing has not been charged for yet (draw()
  // spends it) — the intro's copy of GLGame::camera_delta_pending_.
  int camera_delta_pending_ = 0;

  static const int auto_start_ms = 5000;  // auto-start if nothing pressed
  static const int input_delay_ms = 300;  // ignore held-over shoot input

  GLGame *game;            // owned unless handed_back
  bool handed_back = false;  // dismiss() ran: the StateManager owns the game
  Kind kind;
  const char *name;
  // Owned; display-only (Kind ASTEROID). Row positions are computed in
  // draw(), not the ctor: they depend on the window aspect, and `window`
  // is only set once the StateManager installs the state and resize() runs.
  std::vector<Asteroid *> asteroids_;
  int hazard_kind;     // Hazard::Kind to focus on (Kind HAZARD), else -1
  // Owned; display-only (Kinds INTERCEPTOR, BOMBER and RAMMER): the station
  // deploys the real ones mid-level, so unlike the station/mini-station
  // intros there is no live world object to point the camera at — the
  // intro shows its own hull at actual size, silenced and never stepped
  // (its Follower would chase the frozen world's players; thrust moves
  // nothing). It holds a thrusting pose and only its exhaust trail
  // animates (step_trails); the bomber cruises slower, being a barge.
  GLEnemy *display_enemy_ = nullptr;
  int time = 0;        // ms since the intro appeared (drives flash + spin)
  int step_accum = 0;  // accumulates delta into fixed steps for the spin
  bool unfocused = false;  // freeze the auto-start countdown while unfocused
  bool paused = false;     // pause key held the intro (countdown/tune frozen)
  Mix_Chunk *music_sound = NULL;
  int music_channel = -1;  // looping intro tune; halted on dismissal
};

#endif
