#include "typer.h"
#include "asset_path.h"
#include "highscore.h"
#include "glstarfield.h"
#include "glgame.h"
#include "menu.h"
#include "menu_select.h"
#include "invites.h"
#include "net_lobby.h"
#include "net_policy.h"
#include "net_resume.h"
#include "net_transport.h"
#include "preferences.h"
#include "presence.h"
#include "replay.h"
#include "gl_compat.h"
#include "mat4.h"
#include "steam_build.h"
#include "view/overlay.h"
#include "view/tap_band.h"
#include <ctime>
#include <iostream>
#include <string>

static const float SENSITIVITY_VALUES[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
static const char* SENSITIVITY_LABELS[] = {"SLOW", "LOW", "NORMAL", "HIGH", "MAX"};
static const int NUM_SENSITIVITY = 5;

static const float SMOOTHING_VALUES[] = {0.0f, 0.004f, 0.006f, 0.008f, 0.010f};
static const char* SMOOTHING_LABELS[] = {"OFF", "NORMAL", "HIGH", "HIGHER", "MAX"};
static const int NUM_SMOOTHING = 5;

static const float STAR_DENSITY_MULTIPLIERS[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
static const char* STAR_DENSITY_LABELS[] = {"MINIMAL", "SPARSE", "MEDIUM", "MANY", "FULL"};
static const int NUM_STAR_DENSITY = 5;

// Per-player camera: index 0 = FIXED (view locked to the world), 1 = ROTATE
// (view follows the ship's heading). Stored as PlayerKeys::rotate_view.
static const char* CAMERA_LABELS[] = {"FIXED", "ROTATE"};
static const int NUM_CAMERA = 2;

// Auto-record replays (REPLAY.md): 0 = OFF, 1 = ON. Default OFF is the
// ship posture — this row only makes the pref reachable without hand-editing
// preferences.ini or setting NEWTONIA_REPLAY_ENABLE, which is lost on any
// launch the player does not control (an invite starts a fresh process with
// no intent extras, so recording silently stayed off exactly when testing a
// rejoin).
static const char* RECORD_LABELS[] = {"OFF", "ON"};
static const int NUM_RECORD = 2;

// The Options screen rows, in display order. kind: 0=sensitivity, 1=smoothing,
// 2=camera, 3=star density, 4=auto-record replays. P2 rows are desktop-only
// — mobile (touch) shows Player 1 plus the shared options. Options is
// desktop/controller-only today (see Menu::show_options_row), so the touch
// list is future-proofing.
namespace { struct OptRow { int kind; int player; const char *name; }; }
static const OptRow OPT_ROWS_DESKTOP[] = {
  {0, 0, "P1  SENSITIVITY"}, {1, 0, "P1  SMOOTHING"}, {2, 0, "P1  CAMERA"},
  {0, 1, "P2  SENSITIVITY"}, {1, 1, "P2  SMOOTHING"}, {2, 1, "P2  CAMERA"},
  {3, 0, "STAR  DENSITY"},
  {4, 0, "RECORD  REPLAYS"},
};
// Mobile shows Player 1 + shared options only, so the "P1" prefix is dropped.
static const OptRow OPT_ROWS_TOUCH[] = {
  {0, 0, "SENSITIVITY"}, {1, 0, "SMOOTHING"}, {2, 0, "CAMERA"},
  {3, 0, "STAR DENSITY"},
  {4, 0, "RECORD REPLAYS"},
};
static int opt_row_count() {
  return is_touch_mode() ? (int)(sizeof(OPT_ROWS_TOUCH) / sizeof(OPT_ROWS_TOUCH[0]))
                         : (int)(sizeof(OPT_ROWS_DESKTOP) / sizeof(OPT_ROWS_DESKTOP[0]));
}
static const OptRow &opt_row(int r) {
  return is_touch_mode() ? OPT_ROWS_TOUCH[r] : OPT_ROWS_DESKTOP[r];
}

// Options/replays row-band geometry — shared by the draw and the tap
// hit-test so a tap always lands on the row it appears on. Touch rows fill
// the band above the RETURN TO MENU strip; desktop rows run deeper (no exit
// strip below) — and desktop taps are real input too: the Steam Deck's
// touchscreen reaches the desktop build as pointer clicks (glut.cpp
// forwards them as taps), so the desktop layout needs the same
// draw/hit-test pairing the touch layout has.
static const float TOUCH_OPT_TOP = 250.0f, TOUCH_OPT_BOTTOM = -210.0f;
static const float DESK_OPT_TOP = 250.0f, DESK_OPT_BOTTOM = -300.0f;
static int opt_row_center(int i, int n, float top, float bottom) {
  float pitch = (top - bottom) / n;
  return (int)(top - (i + 0.5f) * pitch);
}
static int opt_row_at(float ny, int n, float top, float bottom) {
  float y = (1.0f - 2.0f * ny) * Typer::scaled_window_height;
  float pitch = (top - bottom) / n;
  float t = top - y;
  if (t < 0 || t >= pitch * n) return -1;
  return (int)(t / pitch);
}
static int touch_opt_center(int i, int n) {
  return opt_row_center(i, n, TOUCH_OPT_TOP, TOUCH_OPT_BOTTOM);
}
static int touch_opt_row_at(float ny, int n) {
  return opt_row_at(ny, n, TOUCH_OPT_TOP, TOUCH_OPT_BOTTOM);
}

// The desktop confirm dialogs (Quit? / New game?) stack Yes above No.
// One geometry definition feeds the draw and the tap hit-test. Returns
// 0 = Yes, 1 = No, -1 = outside the stack (glyphs extend ~2*size below
// their anchor; the two zones pad a half-line outward and meet midway
// between the glyph boxes).
static const int CONFIRM_YES_Y = -40, CONFIRM_NO_Y = -110, CONFIRM_SZ = 22;
static int desktop_confirm_pick(float ny) {
  float y = (1.0f - 2.0f * ny) * Typer::scaled_window_height;
  if (y > CONFIRM_YES_Y + CONFIRM_SZ || y < CONFIRM_NO_Y - 3 * CONFIRM_SZ)
    return -1;
  float boundary = 0.5f * ((CONFIRM_YES_Y - 2 * CONFIRM_SZ) + CONFIRM_NO_Y);
  return y > boundary ? 0 : 1;
}

static int sensitivity_index_for(float value) {
  int best = 2;
  float best_dist = 1e6f;
  for (int i = 0; i < NUM_SENSITIVITY; i++) {
    float d = value > SENSITIVITY_VALUES[i] ? value - SENSITIVITY_VALUES[i]
                                             : SENSITIVITY_VALUES[i] - value;
    if (d < best_dist) { best_dist = d; best = i; }
  }
  return best;
}

static int star_density_index_for(float value) {
  int best = NUM_STAR_DENSITY - 1;
  float best_dist = 1e6f;
  for (int i = 0; i < NUM_STAR_DENSITY; i++) {
    float d = value > STAR_DENSITY_MULTIPLIERS[i] ? value - STAR_DENSITY_MULTIPLIERS[i]
                                                   : STAR_DENSITY_MULTIPLIERS[i] - value;
    if (d < best_dist) { best_dist = d; best = i; }
  }
  return best;
}

static int smoothing_index_for(float value) {
  int best = 2;
  float best_dist = 1e6f;
  for (int i = 0; i < NUM_SMOOTHING; i++) {
    float d = value > SMOOTHING_VALUES[i] ? value - SMOOTHING_VALUES[i]
                                           : SMOOTHING_VALUES[i] - value;
    if (d < best_dist) { best_dist = d; best = i; }
  }
  return best;
}

const int Menu::default_world_width = 5000;
const int Menu::default_world_height = 5000;

Menu::Menu() :
  State(),
  currentTime(0),
  high_score(load_high_score()),
  has_save_(Save::save_exists()),
  menu_selection(0),
  viewpoint(Point(0,default_world_height/2)),
  starfield(new GLStarfield(Point(default_world_width, default_world_height), star_density_scale())) {
  sensitivity_index_[0] = sensitivity_index_for(g_prefs.p1_keys.keyboard_sensitivity);
  sensitivity_index_[1] = sensitivity_index_for(g_prefs.p2_keys.keyboard_sensitivity);
  smoothing_index_[0]   = smoothing_index_for(g_prefs.p1_keys.camera_smoothing);
  smoothing_index_[1]   = smoothing_index_for(g_prefs.p2_keys.camera_smoothing);
  camera_index_[0]      = g_prefs.p1_keys.rotate_view ? 1 : 0;
  camera_index_[1]      = g_prefs.p2_keys.rotate_view ? 1 : 0;
  star_density_index_   = star_density_index_for(g_prefs.star_density);
  auto_record_index_    = g_prefs.auto_record_replays ? 1 : 0;
  scan_replays();
  Presence::set_menu();
#ifdef __EMSCRIPTEN__
  EM_ASM(if (window.setMenuMode) window.setMenuMode(1););
#endif
  if(music == NULL) {
    music = Mix_LoadMUS(asset_path("audio/title.wav").c_str());
    if(music == NULL) {
      std::cout << "Unable to load title.wav (" << Mix_GetError() << ")" << std::endl;
    } else {
      Mix_PlayMusic(music, -1);
    }
  }
}

Menu::~Menu() {
  delete starfield;
  Mix_FreeMusic(music);
}

void Menu::draw() {
  glClear(GL_COLOR_BUFFER_BIT);

  glViewport(0, 0, window.x(), window.y());

  // Perspective starfield: camera at origin looking down -Z, stars have negative z.
  float proj[16];
  mat4_perspective(proj, 90.0f, window.x() / window.y(), 100.0f, 2000.0f);

  float vp[16];
  mat4_translate(vp, proj, -viewpoint.x(), -viewpoint.y(), 0.0f);
  gles2_set_vp(vp);
  starfield->draw_front(viewpoint);
  starfield->draw_rear(viewpoint);

  mat4_translate(vp, proj, -viewpoint.x() + default_world_width, -viewpoint.y(), 0.0f);
  gles2_set_vp(vp);
  starfield->draw_front(viewpoint);
  starfield->draw_rear(viewpoint);

  mat4_translate(vp, proj, -viewpoint.x() - default_world_width, -viewpoint.y(), 0.0f);
  gles2_set_vp(vp);
  starfield->draw_front(viewpoint);
  starfield->draw_rear(viewpoint);

  // Ortho overlay for text (identity model — Typer applies its own transforms via pre_draw).
  // Extents are widened by SAFE_AREA_SCALE so menu text stays inside the
  // TV title-safe region on Xbox (no-op elsewhere).
  float menu_hw = window.x() / Overlay::SAFE_AREA_SCALE;
  float menu_hh = window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -menu_hw, menu_hw, -menu_hh, menu_hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  if (replays_mode_) {
    bool touch = is_touch_mode();
    Typer::draw_centered(0, touch ? 340 : 368, "REPLAYS", touch ? 30 : 26);
    int n = (int)replay_rows_.size();
    // Desktop column x's chosen so the whole row block spans symmetrically
    // about x=0 (the worst-case row — both cursor marks + "CURRENT RUN" + a
    // 6-digit score + LEVEL 99 + date — runs ±623, comfortably inside the
    // ±800 the narrowest aspect ratio shows); a right-heavy set read as
    // off-centre against the centred title. Both marks stand three glyph
    // widths clear of the columns they bracket — squeezed closer, the
    // closing one reads as another character on the end of the date.
    const int CURSOR_L = -623, LABEL_X = -567, SCORE_X = -213, LEVEL_X = 142,
              DATE_X = 377, CURSOR_R = 609;
    for (int i = 0; i < n; i++) {
      const ReplayRow &r = replay_rows_[i];
      char score_buf[24], level_buf[16];
      snprintf(score_buf, sizeof(score_buf), "SCORE %u", r.score);
      snprintf(level_buf, sizeof(level_buf), "LEVEL %u", r.level);
      if (touch) {
        int cy = touch_opt_center(i, n);
        Typer::draw(-500, cy, r.label.c_str(), 16);
        if (r.ok) {
          Typer::draw_centered(60, cy, score_buf, 14);
          Typer::draw(320, cy, r.date.c_str(), 11);
        } else {
          Typer::draw_centered(145, cy, replay_status_text(r.status), 14);
        }
        continue;
      }
      int y = opt_row_center(i, n, DESK_OPT_TOP, DESK_OPT_BOTTOM);
      MenuSelect::draw_row_cursor(replay_sel_ == i, CURSOR_L, CURSOR_R, y, 14);
      Typer::draw(LABEL_X, y, r.label.c_str(), 14);
      if (r.ok) {
        Typer::draw(SCORE_X, y, score_buf, 13);
        Typer::draw(LEVEL_X, y, level_buf, 13);
        Typer::draw(DATE_X, y, r.date.c_str(), 10);
      } else {
        Typer::draw(SCORE_X, y, replay_status_text(r.status), 13);
      }
    }
    // Bottom exit band on BOTH layouts (the lobby's convention): desktop
    // labels the key but stays tappable — the Steam Deck runs the desktop
    // layout and touch needs a way out (field report 2026-07-25).
    // Touch: the band IS the button, so it carries the menu cursor like a
    // selected row. Desktop labels a key instead — a key hint is not a
    // thing you point a cursor at.
    TapBand::return_to_menu.draw(
        touch ? Typer::cursored("RETURN TO MENU", true).c_str()
              : "ESC - BACK TO MENU",
        currentTime);
  } else if (options_mode_) {
    bool touch = is_touch_mode();
    Typer::draw_centered(0, touch ? 340 : 368, "OPTIONS", touch ? 30 : 26);

    int n = opt_row_count();
    // Desktop: one line per option, using the horizontal room — name on the
    // left, numbered choices in the middle, value description on the right.
    // Touch: one big tappable row per option, name left / value right (tap
    // to cycle).
    // Desktop columns (virtual units; the ±200 step span sits well inside
    // the visible width even at 4:3): cursor mark, name left-anchored, step
    // marks centred just right of centre, value left-anchored on the right,
    // closing cursor mark clear of the widest value label. Both marks stand
    // three glyph widths off their column, and the pair spans ±470 — the
    // row block sits centred under the title like the replays list.
    const int CURSOR_L = -470, NAME_X = -422, STEP_CX = 95, STEP_GAP = 50,
              VALUE_X = 265, CURSOR_R = 457;

    for (int row = 0; row < n; row++) {
      const OptRow &r = opt_row(row);

      int num_steps, cur_idx;
      const char* const *lbl;
      switch (r.kind) {
        case 0: num_steps = NUM_SENSITIVITY;  cur_idx = sensitivity_index_[r.player]; lbl = SENSITIVITY_LABELS;   break;
        case 1: num_steps = NUM_SMOOTHING;    cur_idx = smoothing_index_[r.player];   lbl = SMOOTHING_LABELS;     break;
        case 2: num_steps = NUM_CAMERA;       cur_idx = camera_index_[r.player];      lbl = CAMERA_LABELS;        break;
        case 3: num_steps = NUM_STAR_DENSITY; cur_idx = star_density_index_;          lbl = STAR_DENSITY_LABELS;  break;
        default:num_steps = NUM_RECORD;       cur_idx = auto_record_index_;           lbl = RECORD_LABELS;        break;
      }

      if (touch) {
        int cy = touch_opt_center(row, n);
        // Name left, value right. Name font sized so the longest label
        // ("RECORD REPLAYS", ahead of "SENSITIVITY"/"STAR DENSITY") still
        // clears the value column — check this pairing when adding a row.
        Typer::draw(-315, cy, r.name, 16);               // name, left-aligned
        Typer::draw_centered(205, cy, lbl[cur_idx], 18); // value
        continue;
      }

      int y = opt_row_center(row, n, DESK_OPT_TOP, DESK_OPT_BOTTOM);
      MenuSelect::draw_row_cursor(active_row_ == row, CURSOR_L, CURSOR_R, y,
                                12);
      Typer::draw(NAME_X, y, r.name, 12);                // name, left
      const float step_sz = 13.0f;
      for (int i = 0; i < num_steps; i++) {              // numbered choices, mid
        int x = STEP_CX + (int)((i - (num_steps - 1) * 0.5f) * STEP_GAP);
        Typer::draw_centered(x, y, std::to_string(i + 1).c_str(), step_sz);
        if (i == cur_idx) {
          // Hug the selected digit with brackets. Drawn as single glyphs one
          // cell in from the default 2*size character advance, which would
          // otherwise leave "[ 3 ]"; a single-size advance tightens it to "[3]".
          Typer::draw(x - 1.5f * step_sz, y, '[', step_sz);
          Typer::draw(x + 0.5f * step_sz, y, ']', step_sz);
        }
      }
      Typer::draw(VALUE_X, y, lbl[cur_idx], 12);         // value description, right
    }

    // Tappable exit on both layouts — see the replays band note above.
    // Touch: the band IS the button, so it carries the menu cursor like a
    // selected row. Desktop labels a key instead — a key hint is not a
    // thing you point a cursor at.
    TapBand::return_to_menu.draw(
        touch ? Typer::cursored("RETURN TO MENU", true).c_str()
              : "ESC - BACK TO MENU",
        currentTime);
  } else {
    Typer::draw_centered(0, 320, "Newtonia", 80);
    if (high_score > 0) {
      // Touch: the menu rows spread over a taller band (bigger finger
      // targets), so the score block sits lower, above the copyright.
      int hs_y = is_touch_mode() ? -330 : -215;
      Typer::draw_centered(0, hs_y, "HIGH SCORE", 14);
      Typer::draw_centered(0, hs_y - 40, high_score, 18);
    }
  }

  if (!options_mode_ && !replays_mode_) {
    if (attract_mode_) {
      if (!((currentTime / 1400) % 2)) {
        // title_bot=160 (320-2*80), scores_top=-215; center single item in that gap
        const int sz = 18, h = 2 * sz;
        int gap = (160 - (-215) - h) / 2;
        if (is_touch_mode()) {
          Typer::draw_centered(0, 160 - gap, "tap to start", sz);
        } else {
          bool has_ctrl = false;
          int nc = SDL_NumJoysticks();
          for (int i = 0; i < nc; i++) {
            if (SDL_IsGameController(i)) { has_ctrl = true; break; }
          }
          Typer::draw_centered(0, 160 - gap, has_ctrl ? "press start" : "press enter", sz);
        }
      }
    } else if (quit_confirm_) {
      Typer::draw_centered(0, 50, "Quit?", 30);
      if (is_touch_mode()) {
        Typer::draw_centered(-Typer::scaled_window_width / 2, -50, "Yes", 26);
        Typer::draw_centered( Typer::scaled_window_width / 2, -50, "No",  26);
      } else {
        MenuSelect::draw_row(CONFIRM_YES_Y, "Yes", CONFIRM_SZ,
                             quit_selection_ == 0);
        MenuSelect::draw_row(CONFIRM_NO_Y, "No", CONFIRM_SZ,
                             quit_selection_ == 1);
      }
    } else if (new_confirm_) {
      Typer::draw_centered(0, 50, "New game?", 30);
      if (is_touch_mode()) {
        Typer::draw_centered(-Typer::scaled_window_width / 2, -50, "YES", 26);
        Typer::draw_centered( Typer::scaled_window_width / 2, -50, "NO",  26);
      } else {
        MenuSelect::draw_row(CONFIRM_YES_Y, "YES", CONFIRM_SZ,
                             new_selection_ == 0);
        MenuSelect::draw_row(CONFIRM_NO_Y, "NO", CONFIRM_SZ,
                             new_selection_ == 1);
      }
    } else {
      std::vector<std::string> rows;
      if (has_net_resume_) rows.push_back("RESUME HOSTING " + net_resume_code_);
      if (has_save_) rows.push_back("CONTINUE");
      rows.push_back("NEW GAME");
      if (show_online_row()) rows.push_back("ONLINE");
      if (show_options_row()) rows.push_back("OPTIONS");
      if (show_replays_row()) rows.push_back("REPLAYS");
      draw_menu_rows(rows);
    }
  }
  if (!options_mode_ && !replays_mode_)
    Typer::draw_centered(0, -420, "© 2008-2026 METONYMOUS", 13, currentTime);
}

void Menu::tick(int delta) {
  // A friend's invite was accepted (or the game was launched from one):
  // jump straight into the lobby as a joiner for that room code, the same
  // programmatic path as the clipboard/rejoin auto-join.
  {
    std::string invite_code;
    if (Invites::poll_accepted_invite(invite_code)) {
      // Drain the code even when netplay is unavailable/disabled
      // (NEWTONIA_NET_DISABLED web builds): a ?code= deep link must not
      // sit pending forever, and there is no lobby to route it to.
      if (net_available()) {
        request_state_change(new NetLobby(invite_code));
        return;
      }
    }
  }

  scan_net_resume();
  // The reclaim grace ran out while the menu sat open: the room is gone,
  // so the RESUME HOSTING row (and its files) go too.
  if (has_net_resume_ && SDL_GetTicks() >= net_resume_expire_at_) {
    decline_net_resume();
    if (menu_selection >= max_menu_items()) menu_selection = 0;
  }

  currentTime += delta;
  viewpoint += Point(1,0) * (0.025 * delta);
  //FIX: Wrapping bug
  if(viewpoint.x() > default_world_width) {
      viewpoint += Point(-default_world_width,0);
  }

  // Poll R2 directly each tick as a fallback for the first menu load, where
  // SDL may not have sent initial axis-motion events for the trigger yet.
  // The poll feeds the SAME translator (and so the same edge latch) as the
  // event path — a private second latch here made one physical pull confirm
  // twice, once per latch. Max across pads so releasing an idle second pad
  // can't release a latch another pad's held trigger armed.
  int n = SDL_NumJoysticks();
  if (n > 0) {
    Sint16 rt_max = 0;
    SDL_GameController *rt_ctrl = NULL;
    SDL_JoystickID rt_id = -1;
    for(int i = 0; i < n; i++) {
      SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
      if(!ctrl) continue;
      Sint16 v = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
      if(v > rt_max) {
        rt_max = v;
        rt_ctrl = ctrl;
        rt_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctrl));
      }
    }
    SDL_Event e;
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.which = rt_id;
    e.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    e.caxis.value = rt_max;
    unsigned char k = nav_key_from_controller(e);
    if(k) nav_input(k, rt_ctrl);
  }
}

void Menu::controller(SDL_Event event) {
  // Dpad / left stick / A / B / Start / Back / right-trigger all act
  // through the same ladder the keyboard uses — one decision path per
  // screen, so pad directionals and back work wherever keys do. The pad
  // that confirmed rides along so confirm_selection can bind it to P1.
  SDL_GameController *src = NULL;
  unsigned char k = nav_key_from_controller(event, &src);
  if (k) nav_input(k, src);
}

void Menu::keyboard(unsigned char key, int x, int y) {
}

void Menu::keyboard_up(unsigned char key, int x, int y) {
  key = nav_key(key);  // arrows navigate like WASD
#if defined(__ANDROID__) || defined(__IOS__)
  // Touch/mobile — touch_tap and back_pressed() handle ALL interaction
  // (attract dismissal, row selection, confirms). The keys arriving here
  // are only the ones the touch zones synthesize (\r, space, x, p);
  // acting on them would double-handle the tap touch_tap already did.
  // Controller input still works: it arrives via controller(), which
  // feeds nav_input directly and never passes through this filter.
  (void)key;
  return;
#else
#ifdef __EMSCRIPTEN__
  // Touch web: same as mobile — touch_tap owns all interaction and
  // web_menu_tap() synthesizes a stray keyboard_up('\r') per tap.
  if (is_touch_mode()) return;
#endif
  nav_input(key, nullptr);
#endif
}

// The single menu decision ladder (see menu.h). Everything that navigates —
// keyboard, controller buttons/stick, the tick() trigger poll — lands here,
// so each screen's rules exist exactly once.
void Menu::nav_input(unsigned char key, SDL_GameController *src) {
  bool confirm = MenuSelect::is_confirm(key);
  if (attract_mode_) {
    if (confirm) {
      attract_mode_ = false;
    }
#ifndef __EMSCRIPTEN__
    else if (MenuSelect::is_back(key)) {
      attract_mode_ = false;
      quit_confirm_ = true;
      quit_selection_ = 0;
    }
#endif
    return;
  }
  if (options_mode_) {
    if (MenuSelect::move(key, active_row_, opt_row_count())) {
      // moved
    } else if (MenuSelect::is_left(key)) {
      adjust_active_row(-1);
    } else if (MenuSelect::is_right(key)) {
      adjust_active_row(1);
    } else if (MenuSelect::is_back(key) || confirm) {
      close_options();
    }
    return;
  }
  if (replays_mode_) {
    if (MenuSelect::move(key, replay_sel_, (int)replay_rows_.size())) {
      // moved
    } else if (MenuSelect::is_back(key)) {
      replays_mode_ = false;
    } else if (confirm && replay_sel_ < (int)replay_rows_.size()) {
      const ReplayRow &r = replay_rows_[replay_sel_];
      if (r.ok) {
        // The cursor can rest on an unreadable row (damaged / newer
        // version); only readable runs confirm. A NULL here means the file
        // changed underneath us
        // (rotated/deleted) — rescan so the list matches reality.
        if (GLGame *g = GLGame::start_replay_playback(r.path)) {
          request_state_change(g);
          return;
        }
        scan_replays();
        if (replay_rows_.empty()) replays_mode_ = false;
        if (replay_sel_ >= (int)replay_rows_.size()) replay_sel_ = 0;
      }
    }
    return;
  }
  int n = max_menu_items();
  if (quit_confirm_) {
    if (MenuSelect::is_back(key)) {
      quit_confirm_ = false;  // back = No
    } else if (confirm) {
      if (quit_selection_ == 0) {
        glutLeaveMainLoop();
      } else {
        quit_confirm_ = false;
      }
    } else {
      MenuSelect::move(key, quit_selection_, 2);
    }
  } else if (new_confirm_) {
    if (MenuSelect::is_back(key)) {
      new_confirm_ = false;  // back = No, keep the save
    } else if (confirm) {
      if (new_selection_ == 0) {
        confirm_selection(src);
      } else {
        new_confirm_ = false;
      }
    } else {
      MenuSelect::move(key, new_selection_, 2);
    }
  } else {
    if (MenuSelect::is_back(key)) {
      // Quit confirmation is compiled out on web (the browser tab owns
      // closing); Esc/B is a no-op on the root menu there.
#ifndef __EMSCRIPTEN__
      quit_confirm_ = true;
      quit_selection_ = 0;
#endif
    } else if (confirm) {
      if (menu_selection == options_row_index()) {
        open_options();
      } else if (menu_selection == replays_row_index()) {
        open_replays();
      } else {
        confirm_selection(src);
      }
    } else {
      MenuSelect::move(key, menu_selection, n);
    }
  }
}

#if defined(__ANDROID__)
// Defined in android_main.cpp (moveTaskToBack). Plain C++ linkage; the call is
// compiled only in the __ANDROID__ branch below, so no other platform needs
// the symbol.
extern void app_move_to_background();
#endif

bool Menu::back_pressed() {
  if (options_mode_) {
    close_options();  // persists and returns to the menu
    return true;
  }
  if (replays_mode_) {
    replays_mode_ = false;
    return true;
  }
  if (attract_mode_) {
    attract_mode_ = false;
#if defined(_GAMING_XBOX) || defined(_GAMING_DESKTOP)
    quit_confirm_ = true;
    quit_selection_ = 0;
#endif
    return true;
  }
  if (new_confirm_) {
    new_confirm_ = false; // dismiss = No, keep the save
    return true;
  }
  if (quit_confirm_) {
    quit_confirm_ = false; // dismiss = No
    return true;
  }
#if defined(__ANDROID__)
  // Root Back on Android = go Home (background the app, state preserved) — the
  // platform convention, not a quit dialog. focus_lost() auto-saves; the task
  // stays alive so a relaunch resumes on this menu.
  app_move_to_background();
  return true;
#elif defined(__IOS__)
  // Root Back on iOS (hardware-keyboard Escape) = handled but stays put:
  // there is no public "go Home" API, and the quit dialog would be a dead
  // end — its YES calls glutLeaveMainLoop(), a no-op on iOS by design
  // (iOS apps never self-quit). The Home gesture is the way out.
  return true;
#else
  quit_confirm_ = true;
  quit_selection_ = 0;
  return true;
#endif
}

void Menu::touch_tap(float nx, float ny) {
  if (attract_mode_) {
    attract_mode_ = false;  // any tap dismisses the attract screen
    return;
  }
  if (options_mode_) {
    // Tapping a row cycles that option to its next value, wrapping at the
    // end; the bottom strip exits (and persists via close_options) on both
    // layouts — the band is drawn on desktop too, where its zone sits well
    // below the deeper desktop rows (rows end ~-285, band reach tops ~-370).
    if (TapBand::return_to_menu.contains(nx, ny)) { close_options(); return; }
    int row = is_touch_mode()
                  ? touch_opt_row_at(ny, opt_row_count())
                  : opt_row_at(ny, opt_row_count(), DESK_OPT_TOP, DESK_OPT_BOTTOM);
    if (row >= 0) {
      active_row_ = row;
      adjust_active_row(+1, /*wrap=*/true);
    }
    return;
  }
  if (replays_mode_) {
    if (TapBand::return_to_menu.contains(nx, ny)) { replays_mode_ = false; return; }
    int row = is_touch_mode()
                  ? touch_opt_row_at(ny, (int)replay_rows_.size())
                  : opt_row_at(ny, (int)replay_rows_.size(), DESK_OPT_TOP, DESK_OPT_BOTTOM);
    if (row >= 0 && replay_rows_[row].ok) {
      replay_sel_ = row;
      if (GLGame *g = GLGame::start_replay_playback(replay_rows_[row].path)) {
        request_state_change(g);
        return;
      }
      scan_replays();  // file changed underneath us — resync the list
      if (replay_rows_.empty()) replays_mode_ = false;
    }
    return;
  }
  if (quit_confirm_) {
    if (is_touch_mode()) {
      // Left half = Yes (quit), right half = No (dismiss)
      if (nx < 0.5f) {
        glutLeaveMainLoop();
      } else {
        quit_confirm_ = false;
      }
    } else {
      // Desktop stacks Yes above No — hit-test the drawn stack.
      int pick = desktop_confirm_pick(ny);
      if (pick == 0) glutLeaveMainLoop();
      else if (pick == 1) quit_confirm_ = false;
    }
    return;
  }
  if (new_confirm_) {
    if (is_touch_mode()) {
      // Left half = YES (wipe save, start fresh), right half = NO (keep save)
      if (nx < 0.5f) {
        confirm_selection(nullptr);
      } else {
        new_confirm_ = false;
      }
    } else {
      int pick = desktop_confirm_pick(ny);
      if (pick == 0) confirm_selection(nullptr);
      else if (pick == 1) new_confirm_ = false;
    }
    return;
  }
  // Rows are laid out like the desktop menu; hit-test the tapped row.
  int row = menu_row_at(ny);
  if (row < 0) return;
  menu_selection = row;
  if (row == options_row_index()) {
    open_options();
    return;
  }
  if (row == replays_row_index()) {
    open_replays();
    return;
  }
  confirm_selection(nullptr);
}

// The fixed game-entry rows above ONLINE: RESUME HOSTING (when a killed
// hosted session is resumable), CONTINUE (when a solo save exists), and
// NEW GAME. Every later row's index builds on this count.
int Menu::base_menu_rows() const {
  return (has_net_resume_ ? 1 : 0) + (has_save_ ? 2 : 1);
}

int Menu::continue_row_index() const {
  if (!has_save_) return -1;
  return has_net_resume_ ? 1 : 0;
}

int Menu::max_menu_items() const {
  int n = base_menu_rows();
  if (show_online_row()) n++;
  if (show_options_row()) n++;
  if (show_replays_row()) n++;
  return n;
}

bool Menu::show_online_row() const {
  // Build capability AND platform policy (net_policy.h; the default
  // backend always allows — only a platform privilege backend can hide
  // the row on a capable build).
  return net_available() && net_online_play_allowed();
}

bool Menu::show_options_row() const {
  return true;  // always available (was beta-gated)
}

int Menu::menu_row_size() { return is_touch_mode() ? 26 : 22; }

// Bottom of the menu-row band: desktop packs rows above the high-score
// block; touch spreads them over a taller band (they're finger targets)
// and the score block moves down to make room.
static int menu_rows_bottom() { return is_touch_mode() ? -300 : -215; }

// Equally space n row blocks of height h between title_bot=160 and
// menu_rows_bottom(). ONE definition shared by draw_menu_rows and
// menu_row_at so taps always land on what is drawn.
static int menu_row_gap(int n, int h) {
  return (160 - menu_rows_bottom() - n * h) / (n + 1);
}

// Touch draws bigger glyphs and no selection cursor; menu_row_at() mirrors
// this exact geometry so taps land on what is drawn.
void Menu::draw_menu_rows(const std::vector<std::string> &rows) {
  const int sz = menu_row_size(), h = 2 * sz;
  int n = (int)rows.size();
  int gap = menu_row_gap(n, h);
  for (int i = 0; i < n; i++) {
    float y = 160 - (i + 1) * gap - i * h;
    // Touch draws the label bare — no cursor on any touch screen.
    if (is_touch_mode())
      Typer::draw_centered(0, y, rows[i].c_str(), sz);
    else
      MenuSelect::draw_row(y, rows[i], sz, menu_selection == i);
  }
}

int Menu::menu_row_at(float ny) const {
  const int sz = menu_row_size(), h = 2 * sz;
  int n = max_menu_items();
  int gap = menu_row_gap(n, h);
  // The menu ortho maps normalized tap y (0=top, 1=bottom) linearly onto
  // Typer virtual y in [+scaled_window_height, -scaled_window_height].
  float y = (1.0f - 2.0f * ny) * Typer::scaled_window_height;
  // Slot i covers its glyph block plus half a gap either side, so the
  // whole menu band is contiguous finger targets with no dead zones.
  float t = (160.0f - y) - gap * 0.5f;
  if (t < 0) return -1;
  int i = (int)(t / (gap + h));
  return i < n ? i : -1;
}

int Menu::online_row_index() const {
  if (!show_online_row()) return -1;
  return base_menu_rows();  // directly after NEW GAME
}

void Menu::open_options() {
  options_mode_ = true;
}

// Build the replays list from disk (REPLAY.md R3). A readable header makes
// a selectable row with its score/level/date; a file that exists but this
// build can't parse still shows, unselectable, saying WHY (see
// replay_status_text) — the polite decline the plan asks for. Absent files
// make no row, and no rows hides the whole REPLAYS menu row.
// One wording per reason. "OLDER VERSION" used to stand for every failure,
// which was wrong in the only two cases that can actually happen today:
// there has never been more than one replay format, so a file this build
// cannot read is either damaged or from a build newer than this one.
const char *Menu::replay_status_text(int status) {
  switch (status) {
    case Replay::HEADER_TOO_NEW: return "NEWER VERSION";
    case Replay::HEADER_TOO_OLD: return "OLDER VERSION";
    default:                     return "DAMAGED FILE";
  }
}

void Menu::scan_replays() {
  replay_rows_.clear();
  // ONLINE RUN sits last, under the offline slots: current/recent/best are
  // one chain (a run rotates current -> recent, and is best-checked on the
  // way), while online.nrp never rotates through any of them — it is its
  // own slot, overwritten per session. Listing it after the chain rather
  // than inside it matches that.
  struct { const char *label; std::string path; } sources[4] = {
      {"CURRENT RUN", Replay::current_path()},
      {"LAST RUN", Replay::recent_path()},
      {"BEST RUN", Replay::best_path()},
      {"ONLINE RUN", Replay::online_path()},
  };
  for (int i = 0; i < 4; i++) {
    if (sources[i].path.empty()) continue;
    FILE *fp = fopen(sources[i].path.c_str(), "rb");
    if (!fp) continue;
    fclose(fp);
    ReplayRow row;
    row.label = sources[i].label;
    row.path = sources[i].path;
    Replay::Header h;
    row.status = Replay::read_header_status(sources[i].path, h);
    row.ok = row.status == Replay::HEADER_OK;
    if (row.ok) {
      row.score = h.final_score;
      row.level = h.generation + 1;  // displayed level, like everywhere else
      char buf[16] = "";
      time_t t = (time_t)h.date;
      struct tm *tmv = localtime(&t);
      if (tmv) strftime(buf, sizeof(buf), "%Y-%m-%d", tmv);
      row.date = buf;
    } else {
      row.score = 0;
      row.level = 0;
    }
    replay_rows_.push_back(row);
  }
}

int Menu::options_row_index() const {
  if (!show_options_row()) return -1;
  int i = base_menu_rows();
  if (show_online_row()) i++;
  return i;
}

int Menu::replays_row_index() const {
  if (!show_replays_row()) return -1;
  int i = base_menu_rows();
  if (show_online_row()) i++;
  if (show_options_row()) i++;
  return i;
}

void Menu::open_replays() {
  // Rescan on entry: the files change every time a game runs.
  scan_replays();
  if (replay_rows_.empty()) return;  // raced away — row disappears next draw
  replay_sel_ = 0;
  replays_mode_ = true;
}

void Menu::adjust_active_row(int delta, bool wrap) {
  const OptRow &r = opt_row(active_row_);
  int *idx, num;
  switch (r.kind) {
    case 0: idx = &sensitivity_index_[r.player]; num = NUM_SENSITIVITY;  break;
    case 1: idx = &smoothing_index_[r.player];   num = NUM_SMOOTHING;    break;
    case 2: idx = &camera_index_[r.player];      num = NUM_CAMERA;       break;
    case 3: idx = &star_density_index_;          num = NUM_STAR_DENSITY; break;
    default:idx = &auto_record_index_;           num = NUM_RECORD;       break;
  }
  *idx += delta;
  if (wrap) {
    // Touch cycles round; ((v % num) + num) % num handles either direction.
    *idx = ((*idx % num) + num) % num;
  } else {
    if (*idx < 0)    *idx = 0;
    if (*idx >= num) *idx = num - 1;
  }
}

void Menu::close_options() {
  g_prefs.p1_keys.keyboard_sensitivity = SENSITIVITY_VALUES[sensitivity_index_[0]];
  g_prefs.p2_keys.keyboard_sensitivity = SENSITIVITY_VALUES[sensitivity_index_[1]];
  g_prefs.p1_keys.camera_smoothing     = SMOOTHING_VALUES[smoothing_index_[0]];
  g_prefs.p2_keys.camera_smoothing     = SMOOTHING_VALUES[smoothing_index_[1]];
  g_prefs.p1_keys.rotate_view          = (camera_index_[0] == 1);
  g_prefs.p2_keys.rotate_view          = (camera_index_[1] == 1);
  g_prefs.star_density                 = STAR_DENSITY_MULTIPLIERS[star_density_index_];
  g_prefs.auto_record_replays          = (auto_record_index_ == 1);
  save_preferences();
  delete starfield;
  starfield = new GLStarfield(Point(default_world_width, default_world_height),
                              STAR_DENSITY_MULTIPLIERS[star_density_index_]);
  options_mode_ = false;
}

// Host process-death resume (NETPLAY.md): a fresh ticket + online save
// mean a hosted room's process died and its reclaim grace may still be
// open — offer RESUME HOSTING at the top. Expired or mismatched leftovers
// are deleted on sight so a stale row never haunts the menu. Runs on the
// FIRST TICK, not in the constructor: a quit-to-menu constructs the Menu
// before the outgoing GLGame's destructor deletes the files (StateManager
// swaps — and deletes the old state — at the next tick), so a ctor-time
// scan would offer a room that deliberate teardown is about to close.
void Menu::scan_net_resume() {
  if (net_resume_scanned_) return;
  net_resume_scanned_ = true;
  if (!show_online_row()) return;
  std::string code, token;
  long long age_ms = 0;
  if (!NetResume::read(code, token, age_ms)) return;
  if (age_ms < NetResume::GRACE_MS && Save::online_save_exists()) {
    has_net_resume_ = true;
    net_resume_code_ = code;
    net_resume_expire_at_ =
        SDL_GetTicks() + (Uint32)(NetResume::GRACE_MS - age_ms);
  } else {
    NetResume::clear_with_save();
  }
}

// Any other way into a game is a decline of the pending host resume: the
// ticket and online save are deleted (the plan's "decline or expiry"), so
// the row can't reappear after that session ends.
void Menu::decline_net_resume() {
  if (!has_net_resume_) return;
  NetResume::clear_with_save();
  has_net_resume_ = false;
  net_resume_code_.clear();
}

void Menu::confirm_selection(SDL_GameController *ctrl) {
  if (has_net_resume_ && menu_selection == 0) {
    // RESUME HOSTING: rebuild the hosted world from the online save and
    // hand it a game that reclaims the room and awaits the client's
    // auto-rejoin (the resume constructor; see NETPLAY.md).
    std::string code, token;
    long long age_ms = 0;
    Save::GameState s;
    if (NetResume::read(code, token, age_ms) && Save::online_load_game(s)) {
#ifdef __EMSCRIPTEN__
      EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
      request_state_change(new GLGame(s, code, token, ctrl));
      return;
    }
    // Unreadable leftovers: drop the row and stay on the menu.
    decline_net_resume();
    if (menu_selection >= max_menu_items()) menu_selection = 0;
    return;
  }
  if (menu_selection == online_row_index()) {
    decline_net_resume();
    request_state_change(new NetLobby());
    return;
  }
  if (has_save_ && menu_selection == continue_row_index()) {
    Save::GameState s;
    if (Save::load_game(s)) {
#ifdef __EMSCRIPTEN__
      EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
      decline_net_resume();
      request_state_change(new GLGame(s, ctrl));
      return;
    }
    // Corrupt or missing save — fall through to new game
    has_save_ = false;
  }
  // Starting fresh would wipe the existing save — ask first. The YES action
  // re-enters with new_confirm_ set and proceeds.
  if (has_save_ && !new_confirm_) {
    new_confirm_ = true;
    new_selection_ = 1;  // default to NO
    return;
  }
  new_confirm_ = false;
#ifdef __EMSCRIPTEN__
  EM_ASM(if (window.setMenuMode) window.setMenuMode(0););
#endif
  decline_net_resume();
  request_state_change(new GLGame(ctrl));
}
