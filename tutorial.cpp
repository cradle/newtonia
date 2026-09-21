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
static const int   FLASH_MS           = 900;
// Every death costs a respawn, never the tutorial: topped up each tick to
// the Ship ctor's own 4, so the lives row looks exactly as it will in play.
static const int   TUTORIAL_LIVES     = 4;

static const float DEG = (float)M_PI / 180.0f;

Tutorial::Tutorial() {}

const char *Tutorial::step_name(Step s) {
  switch (s) {
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
  step_ = s;
  step_ms_ = 0;
  aligned_ms_ = 0;
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
    case CAMERA:
      beacon_on_ = false;
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
    case SECONDARY:
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
  flash_ms_ = FLASH_MS;
  if (step_ + 1 < STEP_COUNT) enter_step(g, (Step)(step_ + 1));
}

void Tutorial::skip_step(GLGame &g) {
  if (prompt_open_) { apply_camera_choice(g, /*keep=*/true); return; }
  if (step_ == DONE) return;
  complete_step(g);
}

void Tutorial::tick(GLGame &g, int delta) {
  time_ += delta;
  if (flash_ms_ > 0) flash_ms_ -= delta;
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
      if (!s->boost_ready()) complete_step(g);  // the cooldown just started
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
  if (!done_latched_) {
    done_latched_ = true;
    g_prefs.tutorial_done = true;
    save_preferences();
  }
  GLShip *gs = pilot(g);
  PadId pad = gs ? gs->controller_id() : PAD_NONE;
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

// Three STATIONARY rocks fanned out ahead of the ship (a still target is
// the right first target — decided 2026-09-18), the only asteroids the
// tutorial ever spawns. Ordinary killable Asteroids, so hits split them
// and kills credit like any other; num_killable is bumped by the ctor.
void Tutorial::spawn_practice_asteroids(GLGame &g) {
  GLShip *gs = pilot(g);
  if (!gs) return;
  Ship *s = gs->ship;
  Point f = s->facing.normalized();
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

// ---- the CAMERA prompt --------------------------------------------------

TapBand Tutorial::prompt_row(int i) {
  // Two stacked rows under the question; finger pads meet halfway.
  return TapBand(0.5f, i == 0 ? 10.0f : -50.0f, 15, 14);
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

void Tutorial::nav(GLGame &g, unsigned char key) {
  if (!prompt_open_) return;
  if (MenuSelect::move(key, prompt_sel_, 2)) return;
  if (MenuSelect::is_confirm(key)) apply_camera_choice(g, prompt_sel_ == 0);
  else if (MenuSelect::is_back(key)) apply_camera_choice(g, true);
}

bool Tutorial::touch_tap(GLGame &g, float nx, float ny) {
  if (!prompt_open_) return false;
  if (prompt_row(0).contains(nx, ny)) apply_camera_choice(g, true);
  else if (prompt_row(1).contains(nx, ny)) apply_camera_choice(g, false);
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
  // The way out, on every step but the last two (which name it themselves).
  std::string leave = touch ? "" : label(g, A_MENU) + " leaves any time";
  if (skip_on_) {
    leave = touch ? "fly into the red beacon to skip"
                  : "fly into the red beacon to skip - " + leave;
  }
  char buf[96];
  switch (step_) {
    case LAUNCH:
      title = "WELCOME PILOT";
      l1 = one_hand ? "this is your ship - tap anywhere to launch"
         : touch    ? "this is your ship - tap the red circle to launch"
                    : "this is your ship - press " + label(g, A_FIRE) + " to launch";
      l3 = leave;
      break;
    case TURN:
      title = "TURNING";
      l1 = one_hand ? "drag sideways to turn - point at the beacon and hold"
         : touch    ? "drag the joystick left or right - point at the beacon and hold"
         : pad      ? "tilt the " + label(g, A_STEER) + " to turn - point at the beacon and hold"
                    : "turn with " + label(g, A_STEER) + " - point at the beacon and hold";
      snprintf(buf, sizeof buf, "beacon %d of 2%s", beacons_hit_ + 1,
               camera_rounds_ > 0
                   ? (rotate ? " - now the view turns with you"
                             : " - now the view holds still")
                   : "");
      l2 = buf;
      l3 = leave;
      break;
    case THRUST:
      title = "THRUST";
      l1 = one_hand ? "drag upward to thrust - fly through the beacon"
         : touch    ? "push the joystick up to thrust - fly through the beacon"
         : pad      ? "push the " + label(g, A_STEER) + " up to thrust - fly through the beacon"
                    : "thrust with " + label(g, A_THRUST) + " - fly through the beacon";
      l2 = one_hand ? "you keep drifting - drag down to brake"
         : touch    ? "you keep drifting - pull the joystick down to brake"
         : pad      ? "you keep drifting - pull the stick down to brake"
                    : "you keep drifting - brake with " + label(g, A_REVERSE);
      if (thrust_ms_ > 1500) l2 += " - thrusters heat up, watch the bar";
      l3 = leave;
      break;
    case CAMERA:
      title = "CAMERA";
      break;  // the prompt draws itself
    case FIRE: {
      title = "FIRE";
      l1 = one_hand ? "tap to fire - destroy the asteroids"
         : touch    ? "tap the red circle to fire - destroy the asteroids"
                    : "fire with " + label(g, A_FIRE) + " - destroy the asteroids";
      int kills = gs ? gs->ship->asteroid_kills - kills_at_entry_ : 0;
      if (kills > PRACTICE_KILLS) kills = PRACTICE_KILLS;
      snprintf(buf, sizeof buf, "%d of %d destroyed", kills, PRACTICE_KILLS);
      l2 = buf;
      l3 = leave;
      break;
    }
    case BOOST:
      title = "BOOST";
      l1 = touch ? "tap the amber circle to boost - a burst of speed"
                 : "boost with " + label(g, A_BOOST) + " - a burst of speed";
      l2 = "it recharges in two seconds";
      l3 = leave;
      break;
    case SECONDARY:
      title = "SECONDARY WEAPON";
      if (gs && gs->ship->has_secondary())
        l1 = one_hand ? "hold, or tap the blue button, to fire a missile"
           : touch    ? "tap the blue circle to fire a missile"
                      : "fire a missile with " + label(g, A_SECONDARY);
      else
        l1 = "collect the crate ahead - missiles";
      l2 = touch ? "" : label(g, A_NEXT_WEAPON) + " cycles guns, " +
                        label(g, A_NEXT_SECONDARY) + " cycles secondaries";
      l3 = leave;
      break;
    case WRAP:
      title = "ONE MORE THING";
      l1 = touch ? "pause, then CONTROLS, explains the whole layout"
                 : label(g, A_HELP) + " shows every control - " +
                   label(g, A_PAUSE) + " pauses";
      l2 = touch ? "camera: OPTIONS, then CAMERA, on the menu"
                 : label(g, A_ROTATE) + " switches the camera any time";
      l3 = touch ? "tap fire to finish" : "press " + label(g, A_FIRE) + " to finish";
      break;
    case DONE:
      title = "TUTORIAL COMPLETE";
      l1 = touch ? "tap fire to start your first game"
                 : "press " + label(g, A_FIRE) + " to start your first game";
      l2 = touch ? "or pause, then EXIT TO MENU"
                 : label(g, A_MENU) + " for the menu";
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

// A translucent black card behind a block of centred text — the banner
// and the CAMERA prompt read poorly straight over a starfield (field,
// 2026-09-21). Edges in Typer virtual units (a glyph box runs from its
// anchor y down 2*size), converted through Typer::scale to the ortho the
// text is drawn in; a faint outline in the text colour gives the card an
// edge. Drawn BEFORE the text it backs.
static void draw_card(float top, float bottom, float half_w) {
  const float pad_x = 30.0f, pad_y = 14.0f;
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
  glViewport(0, 0, g.window.x(), g.window.y());
  float hw = g.window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = g.window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  GLShip *gs = pilot(g);
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
    const char *question = rotate ? "the view turned with your ship"
                                  : "your ship turned - the view held still";
    const char *rows[2] = {
        rotate ? "KEEP ROTATING CAMERA" : "KEEP FIXED CAMERA",
        rotate ? "TRY FIXED CAMERA" : "TRY ROTATING CAMERA"};
    std::string later = touch
        ? "change it later under OPTIONS, then CAMERA"
        : "switch any time in play with " + label(g, A_ROTATE);
    // The card spans the title's anchor to the hint's glyph floor, as
    // wide as the widest line (a desktop row wears its "> " and " <").
    {
      float w = text_half_w("CAMERA", 22);
      w = std::max(w, text_half_w(question, 9));
      for (int i = 0; i < 2; i++)
        w = std::max(w, text_half_w(Typer::cursored(rows[i], !touch),
                                    prompt_row(i).size));
      w = std::max(w, text_half_w(later, 8));
      draw_card(140.0f, -120.0f - 16.0f, w);
    }
    Typer::draw_centered(0, 140, "CAMERA", 22);
    Typer::draw_centered(0, 85, question, 9);
    for (int i = 0; i < 2; i++) {
      TapBand b = prompt_row(i);
      if (touch)
        Typer::draw_centered(0, b.y, rows[i], b.size);
      else
        MenuSelect::draw_row(b.y, rows[i], b.size, prompt_sel_ == i);
    }
    Typer::draw_centered(0, -120, later.c_str(), 8);
    return;
  }

  std::string title, l1, l2, l3;
  banner_lines(g, title, l1, l2, l3);
  // Under the top HUD row (LEVEL/score), above the ship: title at H-150
  // (size 18 descends 36), then three hint-register lines.
  float H = Typer::scaled_window_height;
  // The lines flow: an empty middle line closes up rather than leaving a
  // hole in the card. Title anchor H-150 (size 18), then a 27-unit pitch.
  const std::string *lines[3] = {&l1, &l2, &l3};
  const float sizes[3] = {9.0f, 9.0f, 8.0f};
  float ys[3];
  int n = 0;
  for (int i = 0; i < 3; i++) {
    if (lines[i]->empty()) continue;
    ys[i] = H - 205 - 27.0f * n;
    n++;
  }
  // The card: from the title's anchor down to the lowest line's glyph
  // floor, as wide as the widest line. Sized on the title, not the GOOD
  // flash, so it holds still while the flash alternates.
  {
    float w = text_half_w(title, 18);
    float bottom = H - 150 - 2 * 18;
    for (int i = 0; i < 3; i++) {
      if (lines[i]->empty()) continue;
      w = std::max(w, text_half_w(*lines[i], sizes[i]));
      bottom = ys[i] - 2 * sizes[i];
    }
    draw_card(H - 150, bottom, w);
  }
  if (flash_ms_ > 0 && (flash_ms_ / 150) % 2 == 0)
    Typer::draw_centered(0, H - 150, "GOOD", 18);
  else
    Typer::draw_centered(0, H - 150, title.c_str(), 18);
  for (int i = 0; i < 3; i++)
    if (!lines[i]->empty())
      Typer::draw_centered(0, ys[i], lines[i]->c_str(), sizes[i]);
}
