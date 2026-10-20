#include "tutorial.h"
#include "glgame.h"
#include "glship.h"
#include "asteroid.h"
#include "missile_pickup.h"
#include "weapon/missile.h"
#include "menu_select.h"
#include "preferences.h"
#include "touch_controls.h"
#include "typer.h"
#include "pad_style.h"
#include "mesh.h"
#include "mat4.h"
#include "gl_compat.h"
#include "gles2_compat.h"
#include "view/overlay.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// The controls the banner names, in the pilot's own vocabulary (label()).
enum { A_LEFT, A_RIGHT, A_STEER, A_THRUST, A_REVERSE, A_FIRE, A_BOOST,
       A_SECONDARY, A_HELP, A_PAUSE, A_ROTATE, A_MENU, A_NEXT_WEAPON,
       A_NEXT_SECONDARY };

// Geometry, in world units. The beacons sit a comfortable screen-fraction
// from the ship (the classic 85-degree view shows ~900 units above the
// ship at z=1000): the turn beacons a third of a screen out, the thrust
// beacon half a screen ahead, and the skip beacon to one side where a
// straight-line launch never crosses it by accident.
static const float TURN_BEACON_DIST   = 320.0f;
static const float TURN_BEACON_ANGLE  = 135.0f;   // degrees off the nose
static const float THRUST_BEACON_DIST = 450.0f;
static const float THRUST_HIT_RADIUS  = 80.0f;
static const float SKIP_BEACON_DIST   = 650.0f;
static const float SKIP_HIT_RADIUS    = 60.0f;
// The nose must be HELD on the beacon, not swept across it: a key held
// down spins the ship straight through the window, and the lesson is
// stopping a turn where you mean to. Headless check: ~13 short taps of
// the turn key reach it; a 12 s continuous hold never does.
static const float ALIGN_COS          = 0.985f;   // ~10 degrees either side
static const int   ALIGN_HOLD_MS      = 150;
static const int   PRACTICE_ROCKS     = 3;
static const int   PRACTICE_KILLS     = 3;
static const float CRATE_DIST         = 260.0f;
// Every death costs a respawn, never the tutorial: topped up each tick to
// the Ship ctor's own 4, so the lives row looks exactly as it will in play.
static const int   TUTORIAL_LIVES     = 4;
// A card must be on screen a moment before a press can answer it: the
// pilot is usually still shooting leftover fragments when WRAP opens, and
// the shot after that used to clear COMPLETE and start level 1 before
// either card was read. Presses inside the window are absorbed.
static const int   CARD_ARM_MS        = 1000;
// The prompts' twin: a key or tap already on its way (the space that
// fired the last shot, a double tap on INPUT) must not answer the next.
static const int   PROMPT_ARM_MS      = 500;

static const float DEG = (float)M_PI / 180.0f;

// The distances above are tuned for a landscape window, where the view
// reaches ~900 units every way. A portrait phone shows only ~420 units
// either side of the ship, so the skip beacon (650 off the nose's right)
// was never on screen, and the practice rocks (420 to the side) sat on
// the edge. Cap a placement at a fraction of the NARROWER half-extent of
// the pilot's view (the camera_screen_radius maths, one axis at a time).
static float view_half_min(Point window, const GLShip *gs) {
  float half_h = tanf(gs->view_angle() * (float)M_PI / 360.0f) * 1000.0f;
  if (window.y() <= 0.0f) return half_h;
  float half_w = half_h * (window.x() / window.y());
  return std::min(half_w, half_h);
}

Tutorial::Tutorial() {
  // Touch opens on the layout question; the sim is frozen under it from
  // the first tick (nothing is pressed yet, so no controls to release).
  if (is_touch_mode()) {
    step_ = INPUT;
    open_prompt();
    prompt_sel_ = touch_one_handed() ? 0 : 1;
    SDL_Log("tutorial: step INPUT");
  }
}

const char *Tutorial::step_name(Step s) {
  switch (s) {
    case INPUT:     return "INPUT";
    case HAND:      return "HAND";
    case LAUNCH:    return "LAUNCH";
    case TURN:      return "TURN";
    case THRUST:    return "THRUST";
    case CAMERA:    return "CAMERA";
    case FIRE:      return "FIRE";
    case BOOST:     return "BOOST";
    case SECONDARY: return "SECONDARY";
    case WRAP:      return "WRAP";
    case DONE:      return "DONE";
    default:        return "?";
  }
}

GLShip *Tutorial::pilot(const GLGame &g) {
  return g.players->empty() ? NULL : g.players->front();
}

bool Tutorial::pad_pilot(const GLGame &g) const {
  GLShip *gs = pilot(g);
  return gs && gs->using_pad();
}

// ---- step machine ---------------------------------------------------

void Tutorial::enter_step(GLGame &g, Step s) {
  // No camera question on touch (field, 2026-09-21): the phone keeps the
  // pref it has, the setting lives under OPTIONS > CAMERA (the wrap-up
  // names it), and the calibration re-run goes with the prompt — THRUST
  // hands straight on to FIRE.
  if (s == CAMERA && is_touch_mode()) s = FIRE;
  // And the layout questions only exist on touch.
  if ((s == INPUT || s == HAND) && !is_touch_mode()) s = LAUNCH;
  step_ = s;
  prompt_pressed_.clear();
  step_ms_ = 0;
  aligned_ms_ = 0;
  // Only TURN and THRUST fly at a beacon; every other step starts with
  // none (it used to be cleared on CAMERA's entry alone, so the touch
  // path — THRUST straight to FIRE — kept the thrust beacon on screen
  // through the practice rocks; field, 2026-09-21).
  beacon_on_ = false;
  GLShip *gs = pilot(g);
  SDL_Log("tutorial: step %s", step_name(s));
  switch (s) {
    case TURN:
      beacons_hit_ = 0;
      // Off to one side, then the other: two turns, one each way — the
      // look-up / look-down of a look-inversion check.
      place_beacon(g, TURN_BEACON_ANGLE, TURN_BEACON_DIST);
      break;
    case THRUST:
      thrust_ms_ = 0;
      place_beacon(g, 0.0f, THRUST_BEACON_DIST);  // straight ahead
      break;
    case INPUT:
      open_prompt();
      prompt_sel_ = touch_one_handed() ? 0 : 1;
      g.release_player_controls();
      break;
    case HAND: {
      open_prompt();
      // The cursor marks the setting in force: L/C/R under one hand;
      // under two hands CENTRE and RIGHT are the same arrangement, so
      // both read as the RIGHT row.
      int hand = g_prefs.touch_handedness;
      if (hand < 0 || hand > 2) hand = 1;
      prompt_sel_ = touch_one_handed() ? hand : (hand == 0 ? 0 : 1);
      g.release_player_controls();
      break;
    }
    case CAMERA:
      open_prompt();
      prompt_sel_ = 0;
      // The prompt takes the keys; a thrust held into it must not stay
      // latched under the frozen sim (the pause's own rule).
      g.release_player_controls();
      break;
    case FIRE:
      if (gs) kills_at_entry_ = gs->ship->asteroid_kills;
      spawn_practice_asteroids(g);
      break;
    case BOOST:
      if (gs) boosts_at_entry_ = gs->ship->net_boost_count;
      break;
    case SECONDARY:
      // Practice has no persisted weapon achievements. Start this lesson's
      // measurement afresh: a random drop fired during FIRE is not an
      // answer to the SECONDARY prompt that has only just appeared.
      if (gs) gs->ship->weapons_fired_mask = 0;
      spawn_crate(g);
      break;
    case WRAP:
      if (gs) shots_at_entry_ = gs->net_shoot_press_count;
      // Reaching the wrap-up IS completing the tutorial: from here the
      // start screen is the full menu, whatever the pilot does next.
      if (!done_latched_) {
        done_latched_ = true;
        g_prefs.tutorial_done = true;
        save_preferences();
      }
      break;
    case DONE:
      if (gs) shots_at_entry_ = gs->net_shoot_press_count;
      break;
    default:
      break;
  }
}

void Tutorial::complete_step(GLGame &g) {
  if (g.pickup_sound) Mix_PlayChannel(-1, g.pickup_sound, 0);
  if (step_ + 1 < STEP_COUNT) enter_step(g, (Step)(step_ + 1));
}

void Tutorial::skip_step(GLGame &g) {
  if (prompt_open_) { prompt_pick(g, -1); return; }  // keep what is set
  if (step_ == DONE) return;
  complete_step(g);
}

void Tutorial::tick(GLGame &g, int delta) {
  time_ += delta;
  if (prompt_open_) return;  // the sim is frozen under the CAMERA prompt
  step_ms_ += delta;
  GLShip *gs = pilot(g);
  if (!gs) return;
  Ship *s = gs->ship;
  // A collision costs a respawn, never the tutorial.
  if (s->lives < TUTORIAL_LIVES) s->lives = TUTORIAL_LIVES;

  // The skip beacon: placed beside the ship at launch and left there —
  // fly into it at any point and the first level starts.
  if (skip_on_ && s->is_alive() &&
      s->position.distance_to(skip_beacon_) < SKIP_HIT_RADIUS) {
    SDL_Log("tutorial: skipped via the skip beacon");
    finish(g);
    return;
  }

  switch (step_) {
    case LAUNCH:
      if (s->is_alive()) {
        // Off the nose's right hand, where the two calibration turns
        // (135 degrees each way) and the straight thrust run never lead.
        Point f = s->facing.normalized();
        float dist = std::min(SKIP_BEACON_DIST, 0.7f * view_half_min(g.window, gs));
        float sx = s->position.x() + f.y() * dist;
        float sy = s->position.y() - f.x() * dist;
        skip_beacon_ = WrappedPoint(sx, sy);
        skip_beacon_.wrap();
        skip_on_ = true;
        complete_step(g);
      }
      break;
    case TURN:
      if (!s->is_alive()) { aligned_ms_ = 0; break; }
      if (nose_on_beacon(g)) {
        aligned_ms_ += delta;
        if (aligned_ms_ >= ALIGN_HOLD_MS) {
          beacons_hit_++;
          aligned_ms_ = 0;
          if (beacons_hit_ >= 2) {
            complete_step(g);
          } else {
            if (g.pickup_sound) Mix_PlayChannel(-1, g.pickup_sound, 0);
            place_beacon(g, -TURN_BEACON_ANGLE, TURN_BEACON_DIST);
          }
        }
      } else {
        aligned_ms_ = 0;
      }
      break;
    case THRUST:
      if (s->thrusting) thrust_ms_ += delta;
      if (s->is_alive() &&
          s->position.distance_to(beacon_) < THRUST_HIT_RADIUS)
        complete_step(g);
      break;
    case CAMERA:
      break;  // the prompt (nav/touch_tap) moves the machine on
    case FIRE:
      if (s->asteroid_kills - kills_at_entry_ >= PRACTICE_KILLS) {
        complete_step(g);
      } else if (Asteroid::num_killable == 0 && step_ms_ > 1000) {
        // Every practice rock gone short of the count (a rock destroyed
        // by something other than the gun pays no kill): another set.
        spawn_practice_asteroids(g);
      }
      break;
    case BOOST:
      if (s->net_boost_count != boosts_at_entry_) complete_step(g);
      break;
    case SECONDARY: {
      const uint32_t secondary_bits =
          (1u << (int)Save::WeaponEntry::Kind::Mine) |
          (1u << (int)Save::WeaponEntry::Kind::GigaMine) |
          (1u << (int)Save::WeaponEntry::Kind::Missile) |
          (1u << (int)Save::WeaponEntry::Kind::Shield) |
          (1u << (int)Save::WeaponEntry::Kind::Turret);
      if (s->weapons_fired_mask & secondary_bits) {
        complete_step(g);
      } else if (!s->has_secondary() && !crate_in_world(g) &&
                 step_ms_ > 1000) {
        spawn_crate(g);  // lost (a rock fragment hit it): another one
      }
      break;
    }
    case WRAP:
    case DONE:
      // Presses before the card has been up CARD_ARM_MS are absorbed
      // (the count re-baselines until then), so a fresh press after it
      // is what ends the card.
      if (step_ms_ < CARD_ARM_MS)
        shots_at_entry_ = gs->net_shoot_press_count;
      else if (gs->net_shoot_press_count != shots_at_entry_) {
        if (step_ == WRAP) complete_step(g);
        else finish(g);
      }
      break;
    default:
      break;
  }
}

// Hand the pilot their first real game. The latch is set here too (the
// skip beacon reaches this without passing WRAP), and the pad that drove
// the tutorial drives the game.
void Tutorial::finish(GLGame &g) {
  if (g.is_finished()) return;
  if (!done_latched_) {
    done_latched_ = true;
    g_prefs.tutorial_done = true;
    save_preferences();
  }
  GLShip *gs = pilot(g);
  PadId pad = gs ? gs->controller_id() : PAD_NONE;
  // The next game's ctor resets the process-wide asteroid count. Reap
  // practice rocks BEFORE that reset, or this game's later destructor
  // subtracts them from the new level (three leftovers cleared level 1).
  for (Asteroid *rock : *g.objects) delete rock;
  g.objects->clear();
  for (Asteroid *rock : *g.dead_objects) delete rock;
  g.dead_objects->clear();
  SDL_Log("tutorial: complete - starting the first game");
  g.request_state_change(new GLGame(pad));
}

// ---- beacons ----------------------------------------------------------

void Tutorial::place_beacon(const GLGame &g, float rel_angle_deg, float dist) {
  GLShip *gs = pilot(g);
  if (!gs) return;
  Ship *s = gs->ship;
  Point f = s->facing.normalized();
  float c = cosf(rel_angle_deg * DEG), sn = sinf(rel_angle_deg * DEG);
  // Rotate the nose vector by the relative angle.
  float dx = f.x() * c - f.y() * sn;
  float dy = f.x() * sn + f.y() * c;
  beacon_ = WrappedPoint(s->position.x() + dx * dist,
                         s->position.y() + dy * dist);
  beacon_.wrap();
  beacon_on_ = true;
}

bool Tutorial::nose_on_beacon(const GLGame &g) const {
  GLShip *gs = pilot(g);
  if (!gs || !beacon_on_) return false;
  Ship *s = gs->ship;
  // closest_to gives the ship's image nearest the beacon, so the
  // difference is the un-wrapped bearing.
  Point sn = s->position.closest_to(beacon_);
  Point d(beacon_.x() - sn.x(), beacon_.y() - sn.y());
  float m = d.magnitude();
  if (m < 1.0f) return true;
  Point f = s->facing.normalized();
  float cosang = (d.x() * f.x() + d.y() * f.y()) / m;
  return cosang >= ALIGN_COS;
}

// Three STATIONARY rocks fanned out beside the ship (a still target is
// the right first target — decided 2026-09-18), the only asteroids the
// tutorial ever spawns. Ordinary killable Asteroids, so hits split them
// and kills credit like any other; num_killable is bumped by the ctor.
void Tutorial::spawn_practice_asteroids(GLGame &g) {
  GLShip *gs = pilot(g);
  if (!gs) return;
  Ship *s = gs->ship;
  Point f = s->facing.normalized();
  // THRUST flows straight here on touch; a camera prompt also preserves
  // momentum on desktop. Place the fan beside the current flight path so
  // a coasting pilot has time to read FIRE before hitting a target.
  Point flight = s->velocity.magnitude() > 0.01f ? s->velocity.normalized() : f;
  f = Point(-flight.y(), flight.x());
  const float dist = std::min(420.0f, 0.6f * view_half_min(g.window, gs));
  for (int i = 0; i < PRACTICE_ROCKS; i++) {
    // 20 degrees apart: at 30 the outer rock rose under the banner card
    // on a 720-line window once the cards grew to one instruction a line.
    float a = (i - (PRACTICE_ROCKS - 1) * 0.5f) * 20.0f * DEG;
    float dx = f.x() * cosf(a) - f.y() * sinf(a);
    float dy = f.x() * sinf(a) + f.y() * cosf(a);
    Asteroid *rock = new Asteroid(false);
    rock->radius = 42.0f;
    rock->radius_squared = rock->radius * rock->radius;
    rock->value = 38;  // ~1600 / radius, the fragment rule
    rock->position = WrappedPoint(s->position.x() + dx * dist,
                                  s->position.y() + dy * dist);
    rock->position.wrap();
    rock->velocity = Point(0.0f, 0.0f);
    g.objects->push_back(rock);
  }
  g.grid.update((std::list<Object *> *)g.objects);
}

void Tutorial::spawn_crate(GLGame &g) {
  GLShip *gs = pilot(g);
  if (!gs) return;
  Ship *s = gs->ship;
  Point f = s->facing.normalized();
  WrappedPoint p(s->position.x() + f.x() * CRATE_DIST,
                 s->position.y() + f.y() * CRATE_DIST);
  p.wrap();
  g.pickups->push_back(new MissilePickup(p));
}

bool Tutorial::crate_in_world(const GLGame &g) const {
  for (Pickup *p : *g.pickups)
    if (!p->collected && dynamic_cast<MissilePickup *>(p) != NULL) return true;
  return false;
}

// ---- the prompts (INPUT, CAMERA) -----------------------------------------

// Defined with the drawing below; the prompt's tap rows share it.
static float text_scale();
static float prompt_row_pitch() { return is_touch_mode() ? 100.0f : 60.0f; }

TapBand Tutorial::prompt_row(int i, int n) {
  // Spread touch choices further apart and grow their hit bands with the
  // spacing. Keep a small gap between bands so adjacent choices never
  // overlap. Drawing and hit-testing share these same scaled anchors.
  float s = text_scale();
  float pitch = prompt_row_pitch();
  float y = -20.0f + pitch * (0.5f * (n - 1) - i);
  return TapBand(0.5f, y * s, (int)(15 * s), (pitch * 0.5f - 16) * s);
}

int Tutorial::prompt_row_count() const {
  return step_ == HAND && touch_one_handed() ? 3 : 2;
}

void Tutorial::apply_input_choice(GLGame &g, bool one_hand) {
  prompt_open_ = false;
  if (g_prefs.touch_one_hand != one_hand) {
    g_prefs.touch_one_hand = one_hand;
    save_preferences();
    touch_layout_prefs_changed();  // the ONE apply site (touch_controls.h)
  }
  SDL_Log("tutorial: input %s", one_hand ? "ONE HAND" : "TWO HANDS");
  enter_step(g, HAND);
}

void Tutorial::apply_hand_choice(GLGame &g, int handedness) {
  static const char *NAMES[3] = {"LEFT", "CENTRE", "RIGHT"};
  prompt_open_ = false;
  if (g_prefs.touch_handedness != handedness) {
    g_prefs.touch_handedness = handedness;
    save_preferences();
    touch_layout_prefs_changed();
  }
  SDL_Log("tutorial: hand %s", NAMES[handedness]);
  enter_step(g, LAUNCH);
}

void Tutorial::apply_camera_choice(GLGame &g, bool keep) {
  GLShip *gs = pilot(g);
  prompt_open_ = false;
  if (!gs) { enter_step(g, FIRE); return; }
  if (keep) {
    save_preferences();  // the pref already holds this camera
    enter_step(g, FIRE);
    return;
  }
  // Switch: flip P1's pref (the tutorial pilot is seat 1 — a pad-only seat
  // still reads slot 0's camera prefs, exactly as set_player_keys wires
  // it), snap the camera so the change shows at once, and re-run the two
  // flying steps under the other mode before asking again.
  int slot = gs->keymap_slot() >= 0 ? gs->keymap_slot() : 0;
  bool &pref = g_prefs.player_keys[slot].rotate_view;
  pref = !pref;
  save_preferences();
  gs->set_rotate_view_pref(&pref);
  gs->snap_camera_to_heading();
  camera_rounds_++;
  SDL_Log("tutorial: camera switched to %s", pref ? "ROTATE" : "FIXED");
  enter_step(g, TURN);
}

// Row `row` of the open prompt picked (INPUT: 0 = one hand; HAND: the
// handedness, L/C/R or L/R; CAMERA: 0 = keep); -1 backs out, keeping
// what is set.
void Tutorial::prompt_pick(GLGame &g, int row) {
  switch (step_) {
    case INPUT:
      apply_input_choice(g, row < 0 ? touch_one_handed() : row == 0);
      break;
    case HAND: {
      int hand = g_prefs.touch_handedness;
      if (hand < 0 || hand > 2) hand = 1;
      if (row >= 0) hand = touch_one_handed() ? row : (row == 0 ? 0 : 2);
      apply_hand_choice(g, hand);
      break;
    }
    default:
      apply_camera_choice(g, row <= 0);
      break;
  }
}

bool Tutorial::prompt_armed() const {
  return time_ - prompt_opened_at_ >= PROMPT_ARM_MS;
}

void Tutorial::nav(GLGame &g, unsigned char key) {
  if (!prompt_open_) return;
  if (MenuSelect::move(key, prompt_sel_, prompt_row_count())) return;
  if (!prompt_armed()) return;  // cursor moves count; answers wait
  if (MenuSelect::is_confirm(key)) prompt_pick(g, prompt_sel_);
  else if (MenuSelect::is_back(key)) prompt_pick(g, -1);
}

void Tutorial::key_up(GLGame &g, unsigned char key) {
  if (prompt_pressed_.erase(key)) nav(g, key);
}

bool Tutorial::touch_tap(GLGame &g, float nx, float ny) {
  if (!prompt_open_) return false;
  prompt_pressed_.clear();  // consume the finger's synthesized key release
  touch_release_pending_ = true;
  if (!prompt_armed()) return true;  // consumed, but too soon to answer
  int n = prompt_row_count();
  for (int i = 0; i < n; i++)
    if (prompt_row(i, n).contains(nx, ny)) { prompt_pick(g, i); break; }
  return true;  // the prompt owns every tap while it is up
}

// ---- wording --------------------------------------------------------------

// A pad button as the tutorial copy names it: a face button (A, or a
// PlayStation shape) circled like the physical button, the way the F1
// card draws it (Typer::BUTTON_MARK); a word (LB, START, LEFT STICK)
// stays plain text, as it does there.
static std::string pad_label(const GLShip *gs, PadAction a) {
  const char *s = gs->pad_hint(a);
  if (pad_label_is_face(s)) return std::string(1, Typer::BUTTON_MARK) + s;
  return s;
}

std::string Tutorial::label(const GLGame &g, int action) const {
  GLShip *gs = pilot(g);
  if (gs && gs->using_pad()) {
    switch (action) {
      case A_LEFT: case A_RIGHT: case A_STEER: case A_THRUST: case A_REVERSE:
        return pad_label(gs, PAD_ACT_STEER);
      case A_FIRE:           return pad_label(gs, PAD_ACT_FIRE);
      case A_BOOST:          return pad_label(gs, PAD_ACT_BOOST);
      case A_SECONDARY:      return pad_label(gs, PAD_ACT_SECONDARY);
      case A_HELP:           return pad_label(gs, PAD_ACT_HELP);
      case A_PAUSE:          return pad_label(gs, PAD_ACT_PAUSE);
      case A_ROTATE:         return pad_label(gs, PAD_ACT_ROTATE_VIEW);
      case A_MENU:           return pad_label(gs, PAD_ACT_MENU);
      case A_NEXT_WEAPON:    return pad_label(gs, PAD_ACT_NEXT_WEAPON);
      case A_NEXT_SECONDARY: return pad_label(gs, PAD_ACT_NEXT_SECONDARY);
    }
    return "?";
  }
  int slot = gs && gs->keymap_slot() >= 0 ? gs->keymap_slot() : 0;
  const PlayerKeys &pk = g_prefs.player_keys[slot];
  const GeneralKeys &gk = g_prefs.general_keys;
  switch (action) {
    case A_LEFT:           return GLShip::key_name(pk.left.primary());
    case A_RIGHT:          return GLShip::key_name(pk.right.primary());
    case A_STEER:          return GLShip::key_name(pk.left.primary()) + " and " +
                                  GLShip::key_name(pk.right.primary());
    case A_THRUST:         return GLShip::key_name(pk.thrust.primary());
    case A_REVERSE:        return GLShip::key_name(pk.reverse.primary());
    case A_FIRE:           return GLShip::key_name(pk.shoot.primary());
    case A_BOOST:          return GLShip::key_name(pk.boost.primary());
    case A_SECONDARY:      return GLShip::key_name(pk.mine.primary());
    case A_HELP:           return GLShip::key_name(pk.help.primary());
    case A_PAUSE:          return GLShip::key_name(gk.pause);
    case A_ROTATE:         return GLShip::key_name(pk.toggle_rotate_view.primary());
    case A_MENU:           return GLShip::key_name(gk.menu);
    case A_NEXT_WEAPON:    return GLShip::key_name(pk.next_weapon.primary());
    case A_NEXT_SECONDARY: return GLShip::key_name(pk.next_secondary.primary());
  }
  return "?";
}

void Tutorial::banner_lines(const GLGame &g, std::string &title,
                            std::vector<BannerLine> &body,
                            std::vector<std::string> &footer) const {
  GLShip *gs = pilot(g);
  bool pad = pad_pilot(g);
  // A phone pilot on a Bluetooth pad reads the pad's buttons, not the
  // on-screen circles the pad never touches.
  bool touch = is_touch_mode() && !pad;
  bool one_hand = touch && touch_one_handed();
  bool rotate = gs ? gs->rotate_view() : true;
  // One instruction per line, each a plain sentence naming the control
  // and what it does (field, 2026-10-20: "don't put more than one
  // instruction on one line"). Status lines carry no bullet.
  auto say = [&](const std::string &t) { body.push_back(BannerLine{t, true}); };
  auto note = [&](const std::string &t) { body.push_back(BannerLine{t, false}); };
  // The fire control, as a verb phrase: "press SPACE", "tap the red
  // circle" — every "do X to Y" line on these cards starts with it.
  std::string fire = one_hand ? "tap the screen"
                   : touch    ? "tap the red circle"
                              : "press " + label(g, A_FIRE);
  // The steering control, for the stick-shaped inputs.
  std::string stick = pad ? label(g, A_STEER) : "the joystick";
  // The ways out, smallest and last. Touch has no quit key: the pause
  // screen's EXIT TO MENU is where the DONE card points.
  if (skip_on_ && step_ != WRAP && step_ != DONE)
    footer.push_back("fly into the red beacon to skip");
  if (!touch && step_ != WRAP && step_ != DONE)
    footer.push_back(label(g, A_MENU) + " quits the tutorial");
  char buf[96];
  switch (step_) {
    case LAUNCH:
      title = "WELCOME";
      say(fire + " to launch");
      break;
    case TURN:
      title = "TURN";
      say(one_hand ? "drag left or right to turn"
         : touch    ? "push the joystick left or right to turn"
         : pad      ? "turn with " + stick
                    : "turn with " + label(g, A_LEFT) + " and " +
                      label(g, A_RIGHT));
      // The nose has to STOP on it: a held turn sweeps straight through
      // the window (ALIGN_HOLD_MS), so "point at" alone read as a sweep.
      say("stop with your nose on the blue beacon");
      snprintf(buf, sizeof buf, "beacon %d of 2", beacons_hit_ + 1);
      note(buf);
      // A second lap after TRY on the camera prompt: say why it repeats.
      if (camera_rounds_ > 0)
        note(rotate ? "camera: ROTATING - the view turns with you"
                    : "camera: FIXED - the view stays still");
      break;
    case THRUST:
      title = "THRUST";
      say(one_hand ? "drag up to thrust"
         : (touch || pad) ? "push " + stick + " up to thrust"
                    : "hold " + label(g, A_THRUST) + " to thrust");
      say("fly through the blue beacon");
      say(one_hand ? "drag down to slow down"
         : (touch || pad) ? "pull " + stick + " down to slow down"
                    : "hold " + label(g, A_REVERSE) + " to slow down");
      if (thrust_ms_ > 1500) say("watch the heat bar, bottom left");
      break;
    case CAMERA:
      title = "CAMERA";
      break;  // the prompt draws itself
    case FIRE: {
      title = "FIRE";
      say(fire + " to fire");
      say("destroy 3 asteroids");
      int kills = gs ? gs->ship->asteroid_kills - kills_at_entry_ : 0;
      if (kills > PRACTICE_KILLS) kills = PRACTICE_KILLS;
      snprintf(buf, sizeof buf, "%d of %d destroyed", kills, PRACTICE_KILLS);
      note(buf);
      break;
    }
    case BOOST:
      title = "BOOST";
      say((touch ? std::string("tap the amber circle")
                  : "press " + label(g, A_BOOST)) + " for a burst of speed");
      note("boost recharges in 2 seconds");
      break;
    case SECONDARY: {
      title = "SECONDARY WEAPON";
      Weapon::Base *w = gs ? gs->ship->current_secondary() : NULL;
      if (!w) {
        say("fly into the missile item ahead");
      } else {
        // The tutorial's item is a missile; a random drop from a practice
        // rock can equip something else first, and firing that counts too.
        std::string what = dynamic_cast<Weapon::Missile *>(w) != NULL
                               ? "the missile" : "your secondary weapon";
        say((touch ? std::string("tap the blue circle")
                    : "press " + label(g, A_SECONDARY)) + " to fire " + what);
      }
      break;
    }
    case WRAP:
      title = "MORE CONTROLS";
      if (touch) {
        say("pause > CONTROLS shows the touch layout");
        say("menu > OPTIONS > CAMERA sets the camera");
      } else {
        say(label(g, A_HELP) + " shows every control");
        say(label(g, A_PAUSE) + " pauses");
        say(label(g, A_ROTATE) + " switches the camera");
        say(label(g, A_NEXT_WEAPON) + " changes weapon");
        say(label(g, A_NEXT_SECONDARY) + " changes secondary weapon");
      }
      say(fire + " to continue");
      break;
    case DONE:
      title = "TUTORIAL COMPLETE";
      say(fire + " to start your first game");
      say(touch ? "pause > EXIT TO MENU leaves"
                 : label(g, A_MENU) + " returns to the menu");
      break;
    default:
      break;
  }
}

// ---- drawing --------------------------------------------------------------

// The beacons: a ring with a breathing halo (GL_LINES — never GL_POINTS,
// the field-GPU rule) and a solid centre dot. The turn/thrust beacon in
// the HUD's cool cyan; the skip beacon red, with a cross through it.
static void draw_beacon(float cx, float cy, float phase, bool skip) {
  static MeshBuilder mb;
  static Mesh mesh;
  const int segs = 28;
  float r = 26.0f;
  float halo = r + 8.0f + 6.0f * sinf(phase);
  float cr = skip ? 1.0f : 0.55f, cg = skip ? 0.25f : 0.9f, cb = skip ? 0.2f : 1.0f;
  mb.clear();
  mb.begin(GL_LINES);
  for (int pass = 0; pass < 2; pass++) {
    float rr = pass == 0 ? r : halo;
    mb.color(cr, cg, cb, pass == 0 ? 1.0f : 0.35f);
    for (int i = 0; i < segs; i++) {
      float a0 = i * 2.0f * (float)M_PI / segs;
      float a1 = (i + 1) * 2.0f * (float)M_PI / segs;
      mb.vertex(cx + cosf(a0) * rr, cy + sinf(a0) * rr);
      mb.vertex(cx + cosf(a1) * rr, cy + sinf(a1) * rr);
    }
  }
  if (skip) {
    mb.color(cr, cg, cb, 1.0f);
    float k = r * 0.6f;
    mb.vertex(cx - k, cy - k); mb.vertex(cx + k, cy + k);
    mb.vertex(cx - k, cy + k); mb.vertex(cx + k, cy - k);
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  glLineWidth(1.8f);
  mesh.draw();
  if (!skip) {
    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(cr, cg, cb, 1.0f);
    mb.dot(cx, cy, 4.0f);
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }
}

// Half the ink width of a centred Typer line, in virtual units: n glyphs
// at a 2*size advance with 1*size of ink each span (2n - 1) * size (the
// centring skips apostrophes and button marks, so the count does too).
static float text_half_w(const std::string &t, float size) {
  int n = 0;
  for (size_t i = 0; i < t.size(); i++)
    if (t[i] != '\'' && t[i] != Typer::BUTTON_MARK) n++;
  return n > 0 ? size * (n - 0.5f) : 0.0f;
}

// The cards' text scale. PORTRAIT phones keep the 800 virtual half-width
// and stretch the half-height, so a size tuned for a desktop window is a
// 12 px glyph across a 1080 px phone — unreadable (field, 2026-09-21).
// There the cards draw at twice the size (the touch help card's portrait
// precedent, view/overlay.cpp), capped so the WIDEST line — given as its
// half-width at 1x — still clears the screen edge with the card's own
// padding. Desktop and pad draw at 1.25x (below).
// Landscape touch is the same problem at a smaller ratio (a 1080 px
// phone height over the 1200-unit landscape height: 16 px glyphs), so it
// draws at 1.5x. The banner WRAPS a line the scale would push past the
// edge (wrap_line) rather than shrinking the card, so the size holds;
// the prompts' lines are short by construction and never need it.
static bool portrait() { return Typer::scaled_window_height > 620.0f; }
// The widest a centred line may be (half-width): the screen edge less
// the card's padding and a margin.
static float line_limit() { return Typer::scaled_window_width - 30.0f - 24.0f; }
static float text_scale() {
  if (portrait()) return 2.0f;
  // Desktop and pad draw at 1.25x: at 1x a 1280x720 window set the
  // body lines in 9-unit glyphs, small next to the HUD they sit under
  // (field, 2026-10-20: "simple but easy to read").
  return is_touch_mode() ? 1.5f : 1.25f;
}
// Append `t` to `out` at `size`, split where it would run past the line
// limit: at the " - " nearest the middle (the copy's control/goal seam),
// else at the space nearest the middle. Recursive, so a very long line
// keeps splitting; a single unbreakable word just overruns.
static void wrap_line(const std::string &t, float size,
                      std::vector<std::string> &out, std::vector<float> &sizes,
                      float limit = -1.0f) {
  if (limit < 0.0f) limit = line_limit();
  if (text_half_w(t, size) > limit) {
    size_t mid = t.size() / 2, best = std::string::npos, best_d = t.size();
    const char *seps[2] = {" - ", " "};
    for (int k = 0; k < 2 && best == std::string::npos; k++) {
      size_t sep_len = strlen(seps[k]);
      for (size_t pos = t.find(seps[k]); pos != std::string::npos;
           pos = t.find(seps[k], pos + 1)) {
        size_t d = pos > mid ? pos - mid : mid - pos;
        if (d < best_d) { best_d = d; best = pos; }
      }
      if (best != std::string::npos) {
        wrap_line(t.substr(0, best), size, out, sizes, limit);
        wrap_line(t.substr(best + sep_len), size, out, sizes, limit);
        return;
      }
    }
  }
  out.push_back(t);
  sizes.push_back(size);
}

// The body's bullet: a filled dot in the text colour, a little under a
// glyph's half height, centred on (cx, cy) in Typer virtual units — the
// card's list marker (field, 2026-10-20: "a big point / small circle").
static void draw_bullet(float cx, float cy, float size) {
  static MeshBuilder mb;
  static Mesh mesh;
  const float *c = Typer::text_colour();
  float k = Typer::scale;
  mb.clear();
  mb.begin(GL_TRIANGLES);
  mb.color(c[0], c[1], c[2], 1.0f);
  const int N = 16;
  float r = 0.55f * size * k;
  for (int i = 0; i < N; i++) {
    float a0 = i * 2.0f * (float)M_PI / N, a1 = (i + 1) * 2.0f * (float)M_PI / N;
    mb.vertex(cx * k, cy * k);
    mb.vertex(cx * k + cosf(a0) * r, cy * k + sinf(a0) * r);
    mb.vertex(cx * k + cosf(a1) * r, cy * k + sinf(a1) * r);
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

// A translucent black card behind a block of centred text — the banner
// and the CAMERA prompt read poorly straight over a starfield (field,
// 2026-09-21). Edges in Typer virtual units (a glyph box runs from its
// anchor y down 2*size), converted through Typer::scale to the ortho the
// text is drawn in; a faint outline in the text colour gives the card an
// edge. Drawn BEFORE the text it backs.
static void draw_card(float top, float bottom, float half_w, float s) {
  const float pad_x = 30.0f * s, pad_y = 14.0f * s;
  float k = Typer::scale;
  float x0 = (-half_w - pad_x) * k, x1 = (half_w + pad_x) * k;
  float y0 = (bottom - pad_y) * k, y1 = (top + pad_y) * k;
  static MeshBuilder mb;
  static Mesh mesh;
  mb.clear();
  mb.begin(GL_TRIANGLES);
  mb.color(0.0f, 0.0f, 0.0f, 0.65f);
  mb.vertex(x0, y0); mb.vertex(x1, y0); mb.vertex(x1, y1);
  mb.vertex(x0, y0); mb.vertex(x1, y1); mb.vertex(x0, y1);
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
  // The outline is a glyph stroke: the text colour at full strength, at
  // Typer's own line width and thin core weight (pre_draw's 1.1 * scale
  // and the 0.1 core it pins around every text draw), so the frame reads
  // as part of the lettering rather than a box drawn around it.
  const float *c = Typer::text_colour();
  mb.clear();
  mb.begin(GL_LINES);
  mb.color(c[0], c[1], c[2], 1.0f);
  mb.vertex(x0, y0); mb.vertex(x1, y0);
  mb.vertex(x1, y0); mb.vertex(x1, y1);
  mb.vertex(x1, y1); mb.vertex(x0, y1);
  mb.vertex(x0, y1); mb.vertex(x0, y0);
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  float saved_core = gles2_get_line_core_scale();
  gles2_set_line_core_scale(0.1f);
  glLineWidth(1.1f * k);
  mesh.draw();
  gles2_set_line_core_scale(saved_core);
}

void Tutorial::draw_world(const GLGame &g) const {
  (void)g;
  float phase = time_ * 0.004f;
  if (beacon_on_) draw_beacon(beacon_.x(), beacon_.y(), phase, false);
  if (skip_on_) draw_beacon(skip_beacon_.x(), skip_beacon_.y(), phase, true);
}

void Tutorial::draw(const GLGame &g) const {
  // Pause/help owns the screen until dismissed, including focus-loss
  // pauses while a setup card is open.
  GLShip *gs = pilot(g);
  if (!g.running || g.touch_help_active_ || (gs && gs->showing_help())) return;
  glViewport(0, 0, g.window.x(), g.window.y());
  float hw = g.window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = g.window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  bool touch = is_touch_mode();

  if (prompt_open_) {
    // The question, over the pause menu's dim: the sim is frozen and the
    // rows own input, exactly like that screen.
    static MeshBuilder mb;
    static Mesh mesh;
    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(0.0f, 0.0f, 0.0f, 0.4f);
    mb.vertex(-hw, -hh); mb.vertex(hw, -hh); mb.vertex(hw, hh);
    mb.vertex(-hw, -hh); mb.vertex(hw, hh); mb.vertex(-hw, hh);
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
    bool rotate = gs ? gs->rotate_view() : true;
    bool one_hand = touch_one_handed();
    const char *title, *question;
    const char *rows[3] = {NULL, NULL, NULL};
    std::string later = "change later: pause > CONTROLS";
    switch (step_) {
      case INPUT:
        title = "CONTROLS";
        question = "steer and fire with one thumb, or two?";
        rows[0] = "ONE HAND"; rows[1] = "TWO HANDS";
        break;
      case HAND:
        title = "HANDEDNESS";
        if (one_hand) {
          question = "where does your thumb rest?";
          rows[0] = "LEFT"; rows[1] = "CENTRE"; rows[2] = "RIGHT";
        } else {
          question = "which thumb fires?";
          rows[0] = "LEFT"; rows[1] = "RIGHT";
        }
        break;
      default:
        title = "CAMERA";
        question = rotate ? "the view turned with your ship"
                          : "the view stayed still";
        rows[0] = rotate ? "KEEP ROTATING" : "KEEP FIXED";
        rows[1] = rotate ? "TRY FIXED" : "TRY ROTATING";
        later = touch ? "later: OPTIONS > CAMERA"
                      : label(g, A_ROTATE) + " switches the camera in play";
        break;
    }
    int n = prompt_row_count();
    // The layout rows wear the cursor on touch too: it marks the setting
    // in force, the one a tap elsewhere keeps. The CAMERA rows carry it
    // only where a cursor moves (its question is keep-or-try, not
    // which-is-set).
    bool cursor = step_ != CAMERA || !touch;
    // The card spans the title's anchor to the hint's glyph floor, as
    // wide as the widest line (a cursored row wears its "> " and " <").
    // Every line here is short, so the width cap never bites and the
    // plain text scale is the card's scale — the same one prompt_row
    // sizes the tap rows with.
    float s = text_scale();
    float w = text_half_w(title, 22);
    w = std::max(w, text_half_w(question, 9));
    for (int i = 0; i < n; i++)
      w = std::max(w, text_half_w(Typer::cursored(rows[i], cursor), 15));
    w = std::max(w, text_half_w(later, 8));
    // Expand the card, question and footer around the taller touch rows
    // as well as the optional third handedness choice.
    float grow = (prompt_row_pitch() * (n - 1) - 60.0f) * 0.5f;
    float top_y = 140.0f + grow, hint_y = -120.0f - grow;
    draw_card(top_y * s, (hint_y - 16.0f) * s, w * s, s);
    Typer::draw_centered(0, top_y * s, title, 22 * s);
    Typer::draw_centered(0, (85.0f + grow) * s, question, 9 * s);
    for (int i = 0; i < n; i++) {
      TapBand b = prompt_row(i, n);
      if (cursor)
        MenuSelect::draw_row(b.y, rows[i], b.size, prompt_sel_ == i);
      else
        Typer::draw_centered(0, b.y, rows[i], b.size);
    }
    Typer::draw_centered(0, hint_y * s, later.c_str(), 8 * s);
    return;
  }

  std::string title;
  std::vector<BannerLine> body;
  std::vector<std::string> footer;
  banner_lines(g, title, body, footer);
  // Under the top HUD row (LEVEL/score), above the ship: title at H-TOP
  // (size 18 descends 36), then the body — a LEFT-aligned column, each
  // instruction after a bullet dot — then the footer's ways out, smaller
  // and centred. TOP clears the LEVEL line's glyph floor (and the card's
  // own top padding) with real air even at HUD SIZE LARGEST — at 150 the
  // card crowded it (field, 2026-09-21).
  float H = Typer::scaled_window_height;
  const float s = text_scale(), TITLE = 18.0f * s;
  const float BODY = 9.0f * s, FOOT = 8.0f * s;
  // The bullet's column: the dot sits in one glyph advance, then a gap.
  const float INDENT = 3.0f * BODY;
  // Wrap a body line the scale pushes past the edge (the column is
  // centred, so the limit is the full width less the indent); a wrapped
  // line's continuation carries no bullet of its own.
  std::vector<BannerLine> lines;
  for (size_t i = 0; i < body.size(); i++) {
    std::vector<std::string> parts;
    std::vector<float> unused;
    wrap_line(body[i].text, BODY, parts, unused, line_limit() - INDENT * 0.5f);
    for (size_t k = 0; k < parts.size(); k++)
      lines.push_back(BannerLine{parts[k], body[i].bullet && k == 0});
  }
  std::vector<std::string> feet;
  std::vector<float> feet_sizes;
  for (size_t i = 0; i < footer.size(); i++)
    wrap_line(footer[i], FOOT, feet, feet_sizes);
  // The body column's width: every line indented (status lines too, so
  // the text edges line up), as wide as its widest line.
  float col_w = 0.0f;
  for (size_t i = 0; i < lines.size(); i++)
    col_w = std::max(col_w, INDENT + 2.0f * text_half_w(lines[i].text, BODY));
  float col_x = -col_w * 0.5f;
  // The card is as wide as the widest of the three parts.
  float w = std::max(text_half_w(title, TITLE), col_w * 0.5f);
  for (size_t i = 0; i < feet.size(); i++)
    w = std::max(w, text_half_w(feet[i], feet_sizes[i]));
  // The card's top: 180 under the top edge (at 1x) — and on touch, under
  // the pause circle instead when the card reaches the circle's column,
  // or always in portrait (most cards reach it there at 2x, and a card
  // that held one height across the steps read better than one that
  // hopped with its width). The circle is in pixels from the window's
  // top-left; a virtual unit is Typer::scale/2 px (Overlay's inset
  // conversion), and HANDEDNESS LEFT mirrors the circle, hence the abs.
  float TOP = 180.0f * s;
  if (is_touch_mode()) {
    float k = 2.0f / Typer::scale;
    float pause_x = std::fabs(g_touch_controls.pause_cx - g.window.x() / 2) -
                    g_touch_controls.pause_radius;
    float pause_floor = g_touch_controls.pause_cy + g_touch_controls.pause_radius;
    if (portrait() || w + 30.0f * s > pause_x * k)
      TOP = std::max(TOP, pause_floor * k + 14.0f * s + 24.0f);
  }
  // Title anchor H-TOP, then a 32-unit pitch (at 1x); the footer opens
  // with an extra half pitch of air so the ways out read as apart from
  // the instructions.
  float bottom = H - TOP - 2 * TITLE;
  float y = H - TOP - 55.0f * s;
  std::vector<float> ys(lines.size()), fys(feet.size());
  for (size_t i = 0; i < lines.size(); i++) {
    ys[i] = y;
    bottom = y - 2 * BODY;
    y -= 32.0f * s;
  }
  if (!lines.empty()) y -= 13.0f * s;
  for (size_t i = 0; i < feet.size(); i++) {
    fys[i] = y;
    bottom = y - 2 * feet_sizes[i];
    y -= 24.0f * s;
  }
  draw_card(H - TOP, bottom, w, s);
  // The next step's title lands on the spot: the completion cue is the
  // pickup chime alone, with no interstitial word (cut in the field,
  // 2026-09-21).
  Typer::draw_centered(0, H - TOP, title.c_str(), TITLE);
  for (size_t i = 0; i < lines.size(); i++) {
    if (lines[i].bullet) draw_bullet(col_x + BODY, ys[i] - BODY, BODY);
    Typer::draw(col_x + INDENT, ys[i], lines[i].text.c_str(), BODY);
  }
  for (size_t i = 0; i < feet.size(); i++)
    Typer::draw_centered(0, fys[i], feet[i].c_str(), feet_sizes[i]);
}
