#include "tutorial.h"
#include "glgame.h"
#include "glship.h"
#include "asteroid.h"
#include "missile_pickup.h"
#include "menu_select.h"
#include "preferences.h"
#include "touch_controls.h"
#include "typer.h"
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

static const float DEG = (float)M_PI / 180.0f;

Tutorial::Tutorial() {
  // Touch opens on the layout question; the sim is frozen under it from
  // the first tick (nothing is pressed yet, so no controls to release).
  if (is_touch_mode()) {
    step_ = INPUT;
    prompt_open_ = true;
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
      prompt_open_ = true;
      prompt_sel_ = touch_one_handed() ? 0 : 1;
      g.release_player_controls();
      break;
    case HAND: {
      prompt_open_ = true;
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
      prompt_open_ = true;
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
        float sx = s->position.x() + f.y() * SKIP_BEACON_DIST;
        float sy = s->position.y() - f.x() * SKIP_BEACON_DIST;
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
      if (gs->net_shoot_press_count != shots_at_entry_) complete_step(g);
      break;
    case DONE:
      if (gs->net_shoot_press_count != shots_at_entry_) finish(g);
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
  const float dist = 420.0f;
  for (int i = 0; i < PRACTICE_ROCKS; i++) {
    float a = (i - (PRACTICE_ROCKS - 1) * 0.5f) * 30.0f * DEG;
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

void Tutorial::nav(GLGame &g, unsigned char key) {
  if (!prompt_open_) return;
  if (MenuSelect::move(key, prompt_sel_, prompt_row_count())) return;
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
  int n = prompt_row_count();
  for (int i = 0; i < n; i++)
    if (prompt_row(i, n).contains(nx, ny)) { prompt_pick(g, i); break; }
  return true;  // the prompt owns every tap while it is up
}

// ---- wording --------------------------------------------------------------

std::string Tutorial::label(const GLGame &g, int action) const {
  GLShip *gs = pilot(g);
  if (gs && gs->using_pad()) {
    switch (action) {
      case A_LEFT: case A_RIGHT: case A_STEER: case A_THRUST: case A_REVERSE:
        return gs->pad_hint(PAD_ACT_STEER);
      case A_FIRE:           return gs->pad_hint(PAD_ACT_FIRE);
      case A_BOOST:          return gs->pad_hint(PAD_ACT_BOOST);
      case A_SECONDARY:      return gs->pad_hint(PAD_ACT_SECONDARY);
      case A_HELP:           return gs->pad_hint(PAD_ACT_HELP);
      case A_PAUSE:          return gs->pad_hint(PAD_ACT_PAUSE);
      case A_ROTATE:         return gs->pad_hint(PAD_ACT_ROTATE_VIEW);
      case A_MENU:           return gs->pad_hint(PAD_ACT_MENU);
      case A_NEXT_WEAPON:    return gs->pad_hint(PAD_ACT_NEXT_WEAPON);
      case A_NEXT_SECONDARY: return gs->pad_hint(PAD_ACT_NEXT_SECONDARY);
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
                            std::string &l1, std::string &l2,
                            std::string &l3) const {
  GLShip *gs = pilot(g);
  bool touch = is_touch_mode();
  bool one_hand = touch && touch_one_handed();
  bool pad = pad_pilot(g);
  bool rotate = gs ? gs->rotate_view() : true;
  // Terse by design (field, 2026-09-21): a control, a dash, the goal.
  std::string leave = touch ? "" : label(g, A_MENU) + ": menu";
  if (skip_on_)
    leave = touch ? "red beacon skips"
                  : "red beacon skips - " + leave;
  std::string stick = pad ? label(g, A_STEER) : "joystick";
  char buf[96];
  switch (step_) {
    case LAUNCH:
      title = "WELCOME";
      l1 = one_hand ? "tap to launch"
         : touch    ? "tap the red circle to launch"
                    : "press " + label(g, A_FIRE) + " to launch";
      l3 = leave;
      break;
    case TURN:
      title = "TURN";
      l1 = one_hand ? "drag sideways - point at the beacon"
         : (touch || pad) ? stick + " left or right - point at the beacon"
                    : label(g, A_STEER) + " - point at the beacon";
      snprintf(buf, sizeof buf, "beacon %d of 2%s", beacons_hit_ + 1,
               camera_rounds_ > 0
                   ? (rotate ? " - the view turns with you"
                             : " - the view holds still")
                   : "");
      l2 = buf;
      l3 = leave;
      break;
    case THRUST:
      title = "THRUST";
      l1 = one_hand ? "drag up - fly through the beacon"
         : (touch || pad) ? stick + " up - fly through the beacon"
                    : label(g, A_THRUST) + " - fly through the beacon";
      l2 = one_hand ? "drag down to brake"
         : (touch || pad) ? stick + " down to brake"
                    : label(g, A_REVERSE) + " brakes";
      if (thrust_ms_ > 1500) l2 += " - watch the heat bar";
      l3 = leave;
      break;
    case CAMERA:
      title = "CAMERA";
      break;  // the prompt draws itself
    case FIRE: {
      title = "FIRE";
      l1 = one_hand ? "tap to fire - destroy the asteroids"
         : touch    ? "red circle - destroy the asteroids"
                    : label(g, A_FIRE) + " - destroy the asteroids";
      int kills = gs ? gs->ship->asteroid_kills - kills_at_entry_ : 0;
      if (kills > PRACTICE_KILLS) kills = PRACTICE_KILLS;
      snprintf(buf, sizeof buf, "%d of %d", kills, PRACTICE_KILLS);
      l2 = buf;
      l3 = leave;
      break;
    }
    case BOOST:
      title = "BOOST";
      l1 = touch ? "amber circle - a burst of speed"
                 : label(g, A_BOOST) + " - a burst of speed";
      l2 = "recharges in 2 s";
      l3 = leave;
      break;
    case SECONDARY:
      title = "SECONDARY";
      if (gs && gs->ship->has_secondary())
        l1 = one_hand ? "hold, or the blue button - use the secondary"
           : touch    ? "blue circle - use the secondary"
                      : label(g, A_SECONDARY) + " - use the secondary";
      else
        l1 = "collect the crate ahead";
      l2 = touch ? "" : label(g, A_NEXT_WEAPON) + " and " +
                        label(g, A_NEXT_SECONDARY) + " cycle weapons";
      l3 = leave;
      break;
    case WRAP:
      title = "ALSO";
      l1 = touch ? "pause > CONTROLS shows the layout"
                 : label(g, A_HELP) + " controls - " + label(g, A_PAUSE) +
                   " pause - " + label(g, A_ROTATE) + " camera";
      l2 = touch ? "camera: OPTIONS > CAMERA" : "";
      l3 = touch ? "tap fire to finish" : label(g, A_FIRE) + " to finish";
      break;
    case DONE:
      title = "COMPLETE";
      l1 = touch ? "tap fire to start your first game"
                 : label(g, A_FIRE) + " starts your first game";
      l2 = touch ? "pause > EXIT TO MENU" : label(g, A_MENU) + ": menu";
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
// centring skips apostrophes, so the count does too).
static float text_half_w(const std::string &t, float size) {
  int n = 0;
  for (size_t i = 0; i < t.size(); i++)
    if (t[i] != '\'') n++;
  return n > 0 ? size * (n - 0.5f) : 0.0f;
}

// The cards' text scale. PORTRAIT phones keep the 800 virtual half-width
// and stretch the half-height, so a size tuned for a desktop window is a
// 12 px glyph across a 1080 px phone — unreadable (field, 2026-09-21).
// There the cards draw at twice the size (the touch help card's portrait
// precedent, view/overlay.cpp), capped so the WIDEST line — given as its
// half-width at 1x — still clears the screen edge with the card's own
// padding. Landscape, desktop and pad stay at 1x.
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
  return is_touch_mode() ? 1.5f : 1.0f;
}
// Append `t` to `out` at `size`, split where it would run past the line
// limit: at the " - " nearest the middle (the copy's control/goal seam),
// else at the space nearest the middle. Recursive, so a very long line
// keeps splitting; a single unbreakable word just overruns.
static void wrap_line(const std::string &t, float size,
                      std::vector<std::string> &out, std::vector<float> &sizes) {
  if (text_half_w(t, size) > line_limit()) {
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
        wrap_line(t.substr(0, best), size, out, sizes);
        wrap_line(t.substr(best + sep_len), size, out, sizes);
        return;
      }
    }
  }
  out.push_back(t);
  sizes.push_back(size);
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
        question = "one thumb, or stick and buttons?";
        rows[0] = "ONE HAND"; rows[1] = "TWO HANDS";
        break;
      case HAND:
        title = "HANDEDNESS";
        if (one_hand) {
          question = "which thumb steers?";
          rows[0] = "LEFT"; rows[1] = "CENTRE"; rows[2] = "RIGHT";
        } else {
          question = "which thumb fires?";
          rows[0] = "LEFT"; rows[1] = "RIGHT";
        }
        break;
      default:
        title = "CAMERA";
        question = rotate ? "the view turned with you" : "the view held still";
        rows[0] = rotate ? "KEEP ROTATING" : "KEEP FIXED";
        rows[1] = rotate ? "TRY FIXED" : "TRY ROTATING";
        later = touch ? "later: OPTIONS > CAMERA"
                      : label(g, A_ROTATE) + " switches it in play";
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

  std::string title, l1, l2, l3;
  banner_lines(g, title, l1, l2, l3);
  // Under the top HUD row (LEVEL/score), above the ship: title at H-TOP
  // (size 18 descends 36), then three hint-register lines. TOP clears the
  // LEVEL line's glyph floor (and the card's own top padding) with real
  // air even at HUD SIZE LARGEST — at 150 the card crowded it (field,
  // 2026-09-21).
  float H = Typer::scaled_window_height;
  const float s = text_scale(), TITLE = 18.0f * s;
  // The lines flow at the scale: an empty line closes up rather than
  // leaving a hole in the card, and one the scale pushes past the edge
  // wraps (the touch TURN line at 2x is wider than a portrait phone).
  std::vector<std::string> lines;
  std::vector<float> sizes;
  if (!l1.empty()) wrap_line(l1, 9.0f * s, lines, sizes);
  if (!l2.empty()) wrap_line(l2, 9.0f * s, lines, sizes);
  if (!l3.empty()) wrap_line(l3, 8.0f * s, lines, sizes);
  // The card is as wide as the widest line.
  float w = text_half_w(title, TITLE);
  for (size_t i = 0; i < lines.size(); i++)
    w = std::max(w, text_half_w(lines[i], sizes[i]));
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
  // Title anchor H-TOP, then a 27-unit pitch (at 1x), down to the lowest
  // glyph floor.
  float bottom = H - TOP - 2 * TITLE;
  std::vector<float> ys(lines.size());
  for (size_t i = 0; i < lines.size(); i++) {
    ys[i] = H - TOP - (55 + 27.0f * i) * s;
    bottom = ys[i] - 2 * sizes[i];
  }
  draw_card(H - TOP, bottom, w, s);
  // The next step's title lands on the spot: the completion cue is the
  // pickup chime alone, with no interstitial word (cut in the field,
  // 2026-09-21).
  Typer::draw_centered(0, H - TOP, title.c_str(), TITLE);
  for (size_t i = 0; i < lines.size(); i++)
    Typer::draw_centered(0, ys[i], lines[i].c_str(), sizes[i]);
}
