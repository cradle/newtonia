#ifndef TUTORIAL_H
#define TUTORIAL_H

#include "wrapped_point.h"
#include "point.h"
#include "view/tap_band.h"
#include <string>

class GLGame;
class GLShip;

// The first-time pilot's tutorial: a scripted walk through the controls
// on an EMPTY field, owned by the GLGame it runs inside (GLGame::tutorial_,
// built by GLGame::start_tutorial from the new-player start screen —
// Menu's TUTORIAL row). One step at a time, each with a banner naming
// the control in the pilot's own vocabulary (keyboard binding, pad
// button, or touch gesture) and a completion test read off the ship:
//
//   INPUT      TOUCH ONLY, first: the layout question — ONE HAND or TWO
//              HANDS (Preferences::touch_one_hand), on the CAMERA
//              prompt's card; the pick applies on the spot through the
//              one layout-apply site, so the WELCOME card that follows
//              speaks the chosen layout's gestures. Off touch it is
//              skipped (enter_step forwards it to LAUNCH)
//   HAND       TOUCH ONLY, second: handedness (Preferences::
//              touch_handedness) on the same card — LEFT / CENTRE / RIGHT
//              under ONE HAND (where the ring rests), LEFT / RIGHT under
//              TWO HANDS (which thumb fires; the layout mirrors for LEFT)
//   LAUNCH     press fire (the ordinary first-life READY prompt)
//   TURN       turn the nose onto two beacons, one each side — the camera
//              CALIBRATION, Halo-style: it runs on whatever camera is set
//              (ROTATE by default) so the pilot FEELS the mode before
//              being asked about it
//   THRUST     fly through a third beacon (thrust, drift, brake)
//   CAMERA     the question: keep this camera, or switch? A switch flips
//              the P1 pref, snaps the camera and RE-RUNS the two flying
//              steps under the other mode, then asks again — so the
//              choice is always made after trying the mode it names.
//              Skipped on TOUCH (THRUST goes straight to FIRE): the phone
//              keeps its pref, and the wrap-up names OPTIONS > CAMERA
//   FIRE       three STATIONARY practice asteroids appear beside the
//              ship — the only rocks the tutorial ever spawns
//   BOOST      one boost
//   SECONDARY  a missile crate drops ahead; collect it, fire a missile
//   WRAP       the rest in one card (help, pause, weapon cycling, the
//              camera key); a fire press finishes — tutorial_done latches
//   DONE       fire starts the first real game, the menu key leaves
//
// The game underneath is a real offline GLGame with every persistence path
// cut: nothing it does reaches savegame.dat, highscore.dat, stats.dat or
// the replay slots (GLGame gates those on in_tutorial(), and the ctor
// marks the run suppressed so no achievement or lifetime counter banks).
// Lives are topped up every tick, so a collision costs a respawn, never
// the tutorial. The between-level machinery (clear countdown, rollover,
// intros) is inert here: the field starts empty and stays empty until the
// FIRE step.
class Tutorial {
public:
  enum Step { INPUT = 0, HAND, LAUNCH, TURN, THRUST, CAMERA, FIRE, BOOST,
              SECONDARY, WRAP, DONE, STEP_COUNT };

  Tutorial();

  // Per running tick (GLGame::tick, after the pause gate). Advances the
  // step machine and, under the CAMERA prompt, returns with the sim frozen
  // (owns_input()).
  void tick(GLGame &g, int delta);
  // The beacon, drawn in the object pass (GLGame::draw_objects, after the
  // pickups — world space, the current tile's MVP).
  void draw_world(const GLGame &g) const;
  // The banner / the CAMERA prompt: one full-window ortho pass from
  // GLGame::draw, after the pause chrome.
  void draw(const GLGame &g) const;
  // True while a prompt (INPUT, HAND, CAMERA) is up: the sim is frozen, the
  // ships take no input, and nav keys answer the prompt through nav().
  bool owns_input() const { return prompt_open_; }
  // Prompt navigation in logical keys (State::nav_key / the pad
  // translator's output): up/down move, confirm picks, back = keep.
  void nav(GLGame &g, unsigned char key);
  // Touch: the prompt's two bands. True when the tap was consumed.
  bool touch_tap(GLGame &g, float nx, float ny);
  // Dev/test hook: the skip-level key advances one step (beta builds; the
  // e2e driver walks the machine with it).
  void skip_step(GLGame &g);
  Step step() const { return step_; }
  static const char *step_name(Step s);

private:
  void enter_step(GLGame &g, Step s);
  void complete_step(GLGame &g);      // chime, then the next step
  void finish(GLGame &g);             // latch tutorial_done, start the game
  void place_beacon(const GLGame &g, float rel_angle, float dist);
  bool nose_on_beacon(const GLGame &g) const;
  void spawn_practice_asteroids(GLGame &g);
  void spawn_crate(GLGame &g);
  bool crate_in_world(const GLGame &g) const;
  void apply_camera_choice(GLGame &g, bool keep);
  void apply_input_choice(GLGame &g, bool one_hand);
  void apply_hand_choice(GLGame &g, int handedness);  // 0 L, 1 C, 2 R
  void prompt_pick(GLGame &g, int row);   // the open prompt's row (-1 = back)
  int  prompt_row_count() const;          // rows on the open prompt (2 or 3)
  // The pilot's word for a control: the keyboard binding's name, the pad
  // button's label, per the last input device used.
  std::string label(const GLGame &g, int action) const;
  // The banner's lines for the current step (device-specific wording).
  void banner_lines(const GLGame &g, std::string &title, std::string &l1,
                    std::string &l2, std::string &l3) const;
  bool pad_pilot(const GLGame &g) const;
  static GLShip *pilot(const GLGame &g);
  // Row i of an n-row prompt — ONE definition for draw and tap (TapBand
  // rule); every prompt shares the geometry.
  static TapBand prompt_row(int i, int n);

  Step step_ = LAUNCH;      // INPUT on touch (the ctor)
  int time_ = 0;            // ms in the tutorial (beacon animation)
  int step_ms_ = 0;         // ms in the current step
  // TURN: the beacon and how long the nose has held on it.
  // Explicit origins: the default WrappedPoint ctor rolls a random spot
  // inside bounds that are not set yet when GLGame builds this (SIGFPE).
  bool beacon_on_ = false;
  WrappedPoint beacon_ = WrappedPoint(0.0f, 0.0f);
  int beacons_hit_ = 0;     // 0..2 through TURN
  int aligned_ms_ = 0;
  // The SKIP beacon: placed beside the ship at launch and left there for
  // the rest of the tutorial — fly into it and the first level starts
  // (the impatient pilot's exit, the menu key being the other).
  bool skip_on_ = false;
  WrappedPoint skip_beacon_ = WrappedPoint(0.0f, 0.0f);
  // THRUST: how long the thrusters have run (for the drift line).
  int thrust_ms_ = 0;
  // INPUT / HAND / CAMERA: the prompt.
  bool prompt_open_ = false;
  int prompt_sel_ = 0;      // INPUT: 0 = one hand, 1 = two; HAND: the row
                            // (L/C/R, or L/R under two hands); CAMERA: 0 =
                            // keep, 1 = switch
  int camera_rounds_ = 0;   // switches so far (the wording says "again")
  // FIRE: kills at entry — three more end the step.
  int kills_at_entry_ = 0;
  // WRAP/DONE: the fire-press counter at entry — a FRESH press finishes.
  unsigned char shots_at_entry_ = 0;
  bool done_latched_ = false;
};

#endif
