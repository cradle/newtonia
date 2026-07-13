#ifndef INTRO_H
#define INTRO_H

#include "state.h"
#include <SDL.h>
#include <SDL_mixer.h>

class GLGame;
class Asteroid;

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
  enum Kind { ASTEROID, BLACK_HOLE, MINI_STATION, STATION };

  // Takes ownership of `game` (handed back to the StateManager on dismissal,
  // or deleted — which auto-saves — when leaving to the menu) and of
  // `display_asteroid` (only for Kind ASTEROID, NULL otherwise; display-only,
  // it never enters the game's object lists or collision grid).
  Intro(GLGame *game, Kind kind, const char *name, Asteroid *display_asteroid);
  virtual ~Intro();

  void draw() override;
  void tick(int delta) override;
  void keyboard(unsigned char key, int x, int y) override;
  void keyboard_up(unsigned char key, int x, int y) override;
  void controller(SDL_Event event) override;
  bool back_pressed() override;

  // Called by StateManager (which cannot go through State virtuals for
  // these); controller hot-plug is forwarded to the adopted game so
  // assignments survive the intro.
  void focus_lost();
  void focus_gained();
  void controller_added(SDL_GameController *ctrl);
  void controller_removed(SDL_JoystickID id);

private:
  void dismiss();        // hand the game back to the state manager
  void leave_to_menu();  // abandon the game (deleted in ~Intro, which saves)
  void toggle_pause();   // freeze the auto-start countdown / intro tune
  Point focus() const;

  static const int auto_start_ms = 5000;  // auto-start if nothing pressed
  static const int input_delay_ms = 300;  // ignore held-over shoot input

  GLGame *game;            // owned unless handed_back
  bool handed_back = false;  // dismiss() ran: the StateManager owns the game
  Kind kind;
  const char *name;
  Asteroid *asteroid;  // owned; display-only (Kind ASTEROID)
  int time = 0;        // ms since the intro appeared (drives flash + spin)
  int step_accum = 0;  // accumulates delta into fixed steps for the spin
  bool unfocused = false;  // freeze the auto-start countdown while unfocused
  bool paused = false;     // pause key held the intro (countdown/tune frozen)
  Mix_Chunk *music_sound = NULL;
  int music_channel = -1;  // looping intro tune; halted on dismissal
};

#endif
