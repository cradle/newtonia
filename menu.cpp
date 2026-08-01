#include "typer.h"
#include "asset_path.h"
#include "highscore.h"
#include "glstarfield.h"
#include "glgame.h"
#include "menu.h"
#include "menu_select.h"
#include "invites.h"
#include "net_identity.h"
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
#include <cstring>

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

// Auto-record replays (REPLAY.md): 0 = OFF, 1 = ON. Default is now ON for
// fresh installs (the low-end field pass cleared the recorder on real
// hardware, 2026-07-28), so this row is mostly an opt-OUT — and the reason
// it has to exist at all: NEWTONIA_REPLAY_ENABLE is lost on any launch the
// player does not control (an invite starts a fresh process with no intent
// extras), and hand-editing preferences.ini is not a player-facing answer.
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
  // Must stay LAST: opt_row_count drops it on builds with no leaderboard.
  {5, 0, "LEADERBOARD  PROMPTS"},
};
// Mobile shows Player 1 + shared options only, so the "P1" prefix is dropped.
static const OptRow OPT_ROWS_TOUCH[] = {
  {0, 0, "SENSITIVITY"}, {1, 0, "SMOOTHING"}, {2, 0, "CAMERA"},
  {3, 0, "STAR DENSITY"},
  {4, 0, "RECORD REPLAYS"},
  {5, 0, "LEADERBOARD PROMPT"},  // LAST — see the desktop table
};
static int opt_row_count() {
  int n = is_touch_mode()
              ? (int)(sizeof(OPT_ROWS_TOUCH) / sizeof(OPT_ROWS_TOUCH[0]))
              : (int)(sizeof(OPT_ROWS_DESKTOP) / sizeof(OPT_ROWS_DESKTOP[0]));
  // No leaderboard on this build/session (netless, web-v1, policy-blocked):
  // a prompts toggle for a feature that cannot appear would only confuse.
  if (!net_board_available()) n--;
  return n;
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
// Fixed slots (Typer virtual units). The control pair sits tight under
// the title, the score table runs at constant pitch under its headers,
// and the UPLOAD action + footer hold the bottom — grouping over spread.
static const int BOARD_Y_TOGGLE = 268, BOARD_Y_SEASON = 214,
                 BOARD_Y_HEADER = 158, BOARD_Y_ROW0 = 116,
                 BOARD_ROW_PITCH = 48, BOARD_Y_UPLOAD = -300,
                 BOARD_Y_FOOTER = -364;
// The table shows this many rows at once; the fetch asks for the full
// top-100 board and the window scrolls over it (BOARD_Y_RANGE = the
// "N-M OF R" indicator under the table, tappable on touch: left half
// pages up, right half down).
static const int BOARD_VISIBLE_ROWS = 8, BOARD_Y_RANGE = -262;
// Sentinel for a row entry scrolled out of the window (never drawn,
// never hit-tested — no real slot is anywhere near it).
static const int BOARD_Y_OFFSCREEN = 10000;

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
  leaderboard_index_    = g_prefs.leaderboard_prompts ? 1 : 0;
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
  delete board_net_;  // abandons any in-flight leaderboard fetch/upload
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

  if (board_mode_) {
    bool touch = is_touch_mode();
    Typer::draw_centered(0, touch ? 340 : 368, "LEADERBOARD", touch ? 30 : 26);
    // Entries: [0] the SOLO/CO-OP toggle, [1] the SEASON browser, [2..]
    // the score rows, then the UPLOAD BEST RUN action (the game-over
    // prompt's retry path). Same shared row geometry as the
    // options/replays screens (TapBand rule).
    int n = board_entry_count();
    // Column layout: Typer's advance is 2x the size, so with the row
    // spanning the cursors (-623..609) the right half fits an 8-digit
    // bare score (12: 95..287), "LEVEL 101" (10: 295..475) and an 8-char
    // yy-mm-dd date (8: 476..604) or a NO REPLAY / OTHER VER tag
    // (8: 460..604, right-flush like the date) without the columns
    // running into each other (they did when score and level carried
    // word labels at 13 — "SCORE 900LEVEL...").
    const int CURSOR_L = -623, RANK_X = -560, NAME_X = -455, BADGE_X = -70,
              SCORE_X = 95, LEVEL_X = 295, DATE_X = 476, TAG_X = 460,
              CURSOR_R = 609;
    // Column headers over the score table (structure the bare values lost
    // when the worded labels were dropped for column fit).
    if (!board_rows_.empty()) {
      Typer::draw(RANK_X, BOARD_Y_HEADER, "#", 9);
      Typer::draw(NAME_X, BOARD_Y_HEADER, "PLAYER", 9);
      Typer::draw(SCORE_X, BOARD_Y_HEADER, "SCORE", 9);
      Typer::draw(LEVEL_X, BOARD_Y_HEADER, "LEVEL", 9);
      if (!touch) Typer::draw(DATE_X, BOARD_Y_HEADER, "DATE", 9);
    }
    for (int e = 0; e < n; e++) {
      char text[96];
      if (e == 0) {
        snprintf(text, sizeof(text), "BOARD: %s",
                 board_players_ == 1 ? "SOLO" : "CO-OP");
      } else if (e == 1) {
        // The browsed season; tag which one is this build's ("LIVE") when
        // the browser has somewhere else to go.
        bool own = board_season_ == board_build_season_;
        snprintf(text, sizeof(text), "SEASON: %s%s", board_season_.c_str(),
                 (own && board_seasons_.size() > 1) ? " - LIVE" : "");
      }
      int y = board_entry_y(e);
      if (y == BOARD_Y_OFFSCREEN) continue;  // scrolled out of the window
      if (!touch)
        MenuSelect::draw_row_cursor(board_sel_ == e, CURSOR_L, CURSOR_R, y, 13);
      if (e <= 1) {
        Typer::draw_centered(0, y, text, 14);
        continue;
      }
      int ri = e - 2;
      if (ri < (int)board_rows_.size()) {
        const NetBoard::Row &r = board_rows_[ri];
        // Name display follows the netplay identity rule: an ATTESTED
        // name renders as-is; an iOS row's alias is a CLAIM on an
        // ATTESTED account (Apple has no server-side alias lookup) and
        // renders too (decided with Glenn 2026-08-01): admission already
        // required the real account, the alias passes the same
        // net_sanitize_name boundary as every wire name, and the stakes
        // are display-only — the gamertag rule, deliberately looser than
        // the online-peer identity display (a lobby stranger's claim
        // still never renders there). Only a nameless row falls back to
        // the role label.
        std::string name = net_sanitize_name(r.name);
        if (name.empty()) name = "PLAYER";
        if (!board_best_run_id_.empty() && r.run_id == board_best_run_id_)
          name += " - YOU";
        char rank_buf[12], score_buf[24], level_buf[16];
        snprintf(rank_buf, sizeof(rank_buf), "#%d", r.rank);
        snprintf(score_buf, sizeof(score_buf), "%u", r.score);
        snprintf(level_buf, sizeof(level_buf), "%u", r.generation + 1);
        Typer::draw(RANK_X, y, rank_buf, 13);
        Typer::draw(NAME_X, y, name.c_str(), 12);
        // Platform badge (STEAM/WEB/IOS/ANDROID) so rows from different
        // platforms — and the verified vs claimed distinction above — are
        // visible, not collapsed into an anonymous name.
        const char *badge = net_platform_label(r.platform);
        if (badge && badge[0] && !touch) Typer::draw(BADGE_X, y, badge, 10);
        Typer::draw(SCORE_X, y, score_buf, 12);
        Typer::draw(LEVEL_X, y, level_buf, 10);
        if (!touch) {
          // The last column: the date, unless the replay cannot be watched
          // — retention-demoted (gone) or another version's format (the
          // season browser reaches those) — which matters more than when.
          if (!r.has_replay) {
            Typer::draw(TAG_X, y, "NO REPLAY", 8);
          } else if (!net_board_replay_watchable(r)) {
            Typer::draw(TAG_X, y, "OTHER VER", 8);
          } else if (r.date > 0) {
            char date_buf[16] = "";
            time_t t = (time_t)(r.date / 1000);  // submitted_at is epoch ms
            struct tm *tmv = localtime(&t);
            if (tmv) strftime(date_buf, sizeof(date_buf), "%y-%m-%d", tmv);
            Typer::draw(DATE_X, y, date_buf, 8);
          }
        }
        continue;
      }
      // The UPLOAD BEST RUN action row, phase-labelled.
      switch (board_up_phase_) {
        case 1: {
          // The score stays visible through the transfer and after — the
          // upload is about the score (same rule as the game-over card).
          int pct = board_net_ ? board_net_->transfer_pct() : -1;
          if (pct >= 0)
            snprintf(text, sizeof(text), "UPLOADING %d%% - SCORE %u", pct,
                     board_best_score_);
          else
            snprintf(text, sizeof(text), "UPLOADING - SCORE %u",
                     board_best_score_);
          break;
        }
        case 2:
          snprintf(text, sizeof(text), "UPLOADED #%d - SCORE %u",
                   board_up_rank_, board_best_score_);
          break;
        case 3:
          // Map the worker's terse reason to a player-facing line; benign
          // refusals aren't failures. board_up_reason_ is already
          // sanitized at capture, so an unknown reason is safe to show.
          snprintf(text, sizeof(text), "%s", board_upload_status_text());
          break;
        default:
          snprintf(text, sizeof(text), "UPLOAD BEST RUN - SCORE %u",
                   board_best_score_);
          break;
      }
      Typer::draw_centered(0, y, text, 14);
    }
    // Scroll range under the table when the board exceeds the window —
    // both the "there is more" affordance and, on touch, the pager (tap
    // left half = up, right half = down; see touch_tap).
    if ((int)board_rows_.size() > BOARD_VISIBLE_ROWS) {
      char range[40];
      int last = board_scroll_ + BOARD_VISIBLE_ROWS;
      if (last > (int)board_rows_.size()) last = (int)board_rows_.size();
      snprintf(range, sizeof(range), "%d-%d OF %d", board_scroll_ + 1, last,
               (int)board_rows_.size());
      Typer::draw_centered(0, BOARD_Y_RANGE, range, 9);
    }
    // Status / footer line, anchored under the UPLOAD slot.
    int fy = BOARD_Y_FOOTER;
    int sel_ri = board_sel_ - 2;  // selected score row, or out of range
    const NetBoard::Row *sel_row =
        (sel_ri >= 0 && sel_ri < (int)board_rows_.size())
            ? &board_rows_[sel_ri] : nullptr;
    if (board_error_) {
      Typer::draw_centered(0, fy, "LEADERBOARD UNAVAILABLE", 14);
    } else if (board_loading_) {
      if ((currentTime / 500) % 2)
        Typer::draw_centered(0, fy, "LOADING", 14);
    } else if (board_rows_.empty()) {
      Typer::draw_centered(0, fy, "NO SCORES THIS SEASON", 14);
    } else if (sel_row && !sel_row->has_replay) {
      // Why confirming the highlighted row does nothing (mirrors the
      // row's last-column tag; touch has no cursor, so also its only
      // signal after a dead tap).
      Typer::draw_centered(0, fy, "REPLAY NO LONGER STORED", 14);
    } else if (sel_row && !net_board_replay_watchable(*sel_row)) {
      Typer::draw_centered(0, fy, "REPLAY FROM ANOTHER VERSION", 14);
    } else if (board_your_rank_ > 0 && !board_best_on_board()) {
      // The player's standing whenever their best is NOT already one of
      // the visible rows (rank-of is a projection of the un-uploaded best,
      // so a rank inside the visible range does NOT imply an on-board row
      // — only an actual " - YOU" match does). Shown for both off-board
      // AND would-place-but-not-yet-uploaded bests.
      char yours[32];
      snprintf(yours, sizeof(yours), "YOUR BEST: #%d", board_your_rank_);
      Typer::draw_centered(0, fy, yours, 14);
    }
    TapBand::return_to_menu.draw(
        touch ? Typer::cursored("RETURN TO MENU", true).c_str()
              : "ESC - BACK TO MENU",
        currentTime);
  } else if (replays_mode_) {
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
      int rec_override = -1;  // >=0 on the RECORD REPLAYS row when forced
      const char* const *lbl;
      switch (r.kind) {
        case 0: num_steps = NUM_SENSITIVITY;  cur_idx = sensitivity_index_[r.player]; lbl = SENSITIVITY_LABELS;   break;
        case 1: num_steps = NUM_SMOOTHING;    cur_idx = smoothing_index_[r.player];   lbl = SMOOTHING_LABELS;     break;
        case 2: num_steps = NUM_CAMERA;       cur_idx = camera_index_[r.player];      lbl = CAMERA_LABELS;        break;
        case 3: num_steps = NUM_STAR_DENSITY; cur_idx = star_density_index_;          lbl = STAR_DENSITY_LABELS;  break;
        case 5: num_steps = NUM_RECORD;       cur_idx = leaderboard_index_;           lbl = RECORD_LABELS;        break;
        default:
          num_steps = NUM_RECORD; lbl = RECORD_LABELS;
          // Show the STORED setting, not the override's effective value:
          // the row has to respond when you change it, or a control that
          // ignores you is just a different flavour of the lie this marker
          // exists to prevent. The "(ENV)" suffix carries the truth — an
          // environment variable is overriding this right now, and game
          // start logs which one and which way.
          rec_override = Replay::recording_override();
          cur_idx = auto_record_index_;
          break;
      }

      if (touch) {
        int cy = touch_opt_center(row, n);
        // Name left, value right. Name font sized so the longest label
        // ("RECORD REPLAYS", ahead of "SENSITIVITY"/"STAR DENSITY") still
        // clears the value column — check this pairing when adding a row.
        Typer::draw(-315, cy, r.name, 16);               // name, left-aligned
        Typer::draw_centered(205, cy, lbl[cur_idx], 18); // value
        // Which WAY the env forces it, after the stored value: "OFF ENV ON"
        // reads as "you set OFF, the environment is forcing ON". Smaller
        // than the value so it clears the name column — at value size it
        // would run into "RECORD REPLAYS".
        if (rec_override >= 0)
          Typer::draw(205 + (int)strlen(lbl[cur_idx]) * 18 + 14, cy,
                      rec_override == 1 ? "ENV ON" : "ENV OFF", 11);
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
      // Same note. Size 8 and no parentheses because the value column only
      // spans VALUE_X..CURSOR_R (192 units): "OFF (ENV ON)" overruns the
      // closing cursor mark, "OFF ENV ON" clears it with room to spare.
      if (rec_override >= 0)
        Typer::draw(VALUE_X + (int)strlen(lbl[cur_idx]) * 24 + 8, y,
                    rec_override == 1 ? "ENV ON" : "ENV OFF", 8);
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
    Typer::draw_centered(0, 410, "Newtonia", 80);
    if (high_score > 0) {
      // Touch: the menu rows spread over a taller band (bigger finger
      // targets), so the score block sits lower, above the copyright.
      int hs_y = is_touch_mode() ? -330 : -300;
      Typer::draw_centered(0, hs_y, "HIGH SCORE", 14);
      Typer::draw_centered(0, hs_y - 40, high_score, 18);
    }
  }

  if (!options_mode_ && !replays_mode_ && !board_mode_) {
    if (attract_mode_) {
      if (!((currentTime / 1400) % 2)) {
        // title_bot=250 (410-2*80), scores_top=-330/-320; center in that gap
        const int sz = 18, h = 2 * sz;
        int scores_top = is_touch_mode() ? -330 : -320;
        int gap = (250 - scores_top - h) / 2;
        if (is_touch_mode()) {
          Typer::draw_centered(0, 250 - gap, "tap to start", sz);
        } else {
          bool has_ctrl = false;
          int nc = SDL_NumJoysticks();
          for (int i = 0; i < nc; i++) {
            if (SDL_IsGameController(i)) { has_ctrl = true; break; }
          }
          Typer::draw_centered(0, 250 - gap, has_ctrl ? "press start" : "press enter", sz);
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
      if (show_board_row()) rows.push_back("LEADERBOARD");
      draw_menu_rows(rows);
    }
  }
  if (!options_mode_ && !replays_mode_ && !board_mode_)
    Typer::draw_centered(0, -420, "© 2008-2026 METONYMOUS", 13, currentTime);
}

void Menu::tick(int delta) {
  // Leaderboard screen: drive the fetch/upload socket. May hand the state
  // to replay playback (a downloaded row confirmed) — nothing below
  // depends on running after that, the pending state change just waits
  // for the manager.
  if (board_net_) board_poll();
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
  if (board_mode_) {
    if (MenuSelect::move(key, board_sel_, board_entry_count())) {
      board_ensure_visible();
    } else if (MenuSelect::is_left(key) || MenuSelect::is_right(key)) {
      if (board_sel_ == 1) {
        // On the SEASON row, a/d step the browsed season instead.
        board_cycle_season(MenuSelect::is_left(key) ? -1 : 1);
      } else {
        // Elsewhere a/d switch the SOLO/CO-OP board from anywhere.
        board_players_ = board_players_ == 1 ? 2 : 1;
        board_request();
      }
    } else if (MenuSelect::is_back(key)) {
      close_board();
    } else if (confirm) {
      board_nav_confirm();
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
      } else if (menu_selection == board_row_index()) {
        open_board();
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
  if (board_mode_) {
    close_board();
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
  if (board_mode_) {
    if (TapBand::return_to_menu.contains(nx, ny)) { close_board(); return; }
    // Same fixed slots the draw uses (board_entry_y — the TapBand rule).
    float ty = (1.0f - 2.0f * ny) * Typer::scaled_window_height;
    // The range line doubles as the touch pager: left half pages up,
    // right half pages down (drag scrolling is not a thing this menu
    // stack has; the indicator is the one drawn, tappable affordance).
    if ((int)board_rows_.size() > BOARD_VISIBLE_ROWS &&
        ty <= BOARD_Y_RANGE + BOARD_ROW_PITCH / 2 &&
        ty >= BOARD_Y_RANGE - BOARD_ROW_PITCH / 2) {
      int max_scroll = (int)board_rows_.size() - BOARD_VISIBLE_ROWS;
      board_scroll_ += (nx < 0.5f) ? -BOARD_VISIBLE_ROWS : BOARD_VISIBLE_ROWS;
      if (board_scroll_ < 0) board_scroll_ = 0;
      if (board_scroll_ > max_scroll) board_scroll_ = max_scroll;
      return;
    }
    int row = board_entry_at(ty);
    if (row >= 0) {
      board_sel_ = row;
      board_nav_confirm();
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
  if (row == board_row_index()) {
    open_board();
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
  if (show_board_row()) n++;
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

// Top of the menu-row band: sits below the title (y 410, size 80,
// descending to 250) with deliberate air under it.
static const int MENU_ROWS_TOP = 200;

// Bottom of the menu-row band: desktop packs rows above the high-score
// block; touch spreads them over a taller band (they're finger targets)
// and the score block moves down to make room.
static int menu_rows_bottom() { return is_touch_mode() ? -300 : -280; }

// Equally space n row blocks of height h between MENU_ROWS_TOP and
// menu_rows_bottom(). ONE definition shared by draw_menu_rows and
// menu_row_at so taps always land on what is drawn.
static int menu_row_gap(int n, int h) {
  return (MENU_ROWS_TOP - menu_rows_bottom() - n * h) / (n + 1);
}

// Touch draws bigger glyphs and no selection cursor; menu_row_at() mirrors
// this exact geometry so taps land on what is drawn.
void Menu::draw_menu_rows(const std::vector<std::string> &rows) {
  const int sz = menu_row_size(), h = 2 * sz;
  int n = (int)rows.size();
  int gap = menu_row_gap(n, h);
  for (int i = 0; i < n; i++) {
    float y = MENU_ROWS_TOP - (i + 1) * gap - i * h;
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
  float t = ((float)MENU_ROWS_TOP - y) - gap * 0.5f;
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
    // A readable header is not a watchable run. Playback needs a leading
    // keyframe, so a file with no sim records — a crash, or a web tab
    // closed before the first flush ever reached IndexedDB (the header
    // rides there anyway, since FS.syncfs persists the whole filesystem) —
    // declines at pick time and the row does NOTHING. It listed as a
    // normal CURRENT RUN at score 0, level 1, dated today. Drop it on the
    // same test rotate_to_recent already deletes it by, so the list agrees
    // with what the file is worth. A DAMAGED/NEWER/OLDER row still shows:
    // there the wording is the point.
    if (row.ok && !Replay::has_delta_record(sources[i].path)) continue;
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

// ---- LEADERBOARD screen (LEADERBOARD.md L3) -----------------------------

// Give up polling for a fresh credential after an "unverified" upload
// (mirrors GLGame::BOARD_UPLOAD_RETRY_TIMEOUT_MS — see the credential-
// lifecycle note there; the retry peeks for a fresh credential rather than
// waiting a fixed time, so this is only the give-up deadline).
static const int BOARD_UPLOAD_RETRY_TIMEOUT_MS = 6000;

bool Menu::show_board_row() const { return net_board_available(); }

int Menu::board_row_index() const {
  if (!show_board_row()) return -1;
  int i = base_menu_rows();
  if (show_online_row()) i++;
  if (show_options_row()) i++;
  if (show_replays_row()) i++;
  return i;
}

void Menu::open_board() {
  close_board();
  board_net_ = NetBoard::create();
  if (!board_net_) return;
  // Warm the async platform credential mint so an UPLOAD confirm has it.
  (void)net_board_verify_credential();
  board_net_->connect(net_board_url());
  // The browsed season opens on this build's (the one new runs land in);
  // the SEASON row cycles through every season the worker knows
  // (board_seasons_, fetched below). The upload candidate is best.nrp,
  // which carries its OWN season — the UPLOAD row appears on that
  // season's screen (usually the same one), so an older build's best is
  // uploadable by flipping the SEASON row to it.
  board_build_season_ = Replay::game_version_string();
  board_season_ = board_build_season_;
  board_seasons_.clear();
  NetBoard::Season own;
  own.season = board_build_season_;
  board_seasons_.push_back(own);
  board_net_->seasons();
  board_best_run_id_.clear();
  board_best_season_.clear();
  board_best_score_ = 0;
  board_best_clean_ = false;
  Replay::Header h;
  if (Replay::read_header(Replay::best_path(), h)) {
    char rid[24];
    snprintf(rid, sizeof(rid), "%llu", (unsigned long long)h.run_id);
    board_best_run_id_ = rid;
    board_best_season_.assign(
        h.game_version, strnlen(h.game_version, sizeof(h.game_version)));
    // Seed the browser with best.nrp's season too: the UPLOAD row lives
    // on ITS season's screen, and the worker only lists seasons that
    // already have rows — an old-season best would otherwise be
    // unreachable (nothing to cycle to) until someone else charted there.
    if (!board_best_season_.empty() &&
        board_best_season_ != board_build_season_) {
      NetBoard::Season bs;
      bs.season = board_best_season_;
      board_seasons_.push_back(bs);
    }
    board_best_score_ = h.final_score;
    board_best_clean_ = (h.flags & Replay::FLAG_CLEAN) &&
                        !(h.flags & Replay::FLAG_CHEATED) &&
                        h.final_score > 0;
    board_players_ = h.player_count == 2 ? 2 : 1;
  } else {
    board_players_ = 1;
  }
  board_sel_ = 0;
  board_loading_ = false;
  board_error_ = false;
  board_up_phase_ = 0;
  board_up_retried_ = false;
  board_up_retry_deadline_ = 0;
  board_up_sent_cred_.clear();
  board_fetching_ = false;
  board_mode_ = true;
  board_request();
}

void Menu::close_board() {
  delete board_net_;
  board_net_ = nullptr;
  board_mode_ = false;
}

void Menu::board_request() {
  if (!board_net_) return;
  board_loading_ = true;
  board_error_ = false;
  board_rows_.clear();
  board_your_rank_ = 0;
  if (board_sel_ >= board_entry_count()) board_sel_ = 0;
  board_scroll_ = 0;
  board_net_->top(board_season_, board_players_, 100);  // full board; the
                                                        // table windows it
  // The rank-of footer: only meaningful when the local best belongs to
  // the browsed board (same season, same player count).
  Replay::Header h;
  if (board_best_score_ > 0 && board_best_season_ == board_season_ &&
      Replay::read_header(Replay::best_path(), h)) {
    int best_players = h.player_count == 2 ? 2 : 1;
    if (best_players == board_players_)
      board_net_->rank_of(board_season_, board_players_, board_best_score_);
  }
}

int Menu::board_entry_y(int e) const {
  if (e == 0) return BOARD_Y_TOGGLE;
  if (e == 1) return BOARD_Y_SEASON;
  int ri = e - 2;
  if (ri < (int)board_rows_.size()) {
    if (ri < board_scroll_ || ri >= board_scroll_ + BOARD_VISIBLE_ROWS)
      return BOARD_Y_OFFSCREEN;  // scrolled out of the window
    return BOARD_Y_ROW0 - (ri - board_scroll_) * BOARD_ROW_PITCH;
  }
  return BOARD_Y_UPLOAD;  // the UPLOAD BEST RUN action (always last)
}

// Slide the window so the selected entry is visible (rows only — the
// fixed slots always are). Called after every selection move.
void Menu::board_ensure_visible() {
  int ri = board_sel_ - 2;
  if (ri < 0 || ri >= (int)board_rows_.size()) return;
  if (ri < board_scroll_) board_scroll_ = ri;
  if (ri >= board_scroll_ + BOARD_VISIBLE_ROWS)
    board_scroll_ = ri - BOARD_VISIBLE_ROWS + 1;
}

int Menu::board_entry_at(float y) const {
  int n = board_entry_count();
  for (int e = 0; e < n; e++)
    if (y <= board_entry_y(e) + BOARD_ROW_PITCH / 2 &&
        y >= board_entry_y(e) - BOARD_ROW_PITCH / 2)
      return e;
  return -1;
}

int Menu::board_entry_count() const {
  // [0] SOLO/CO-OP toggle, [1] SEASON browser, [2..] score rows, then the
  // UPLOAD BEST RUN action when it applies to the browsed season.
  return 2 + (int)board_rows_.size() + (board_upload_row_shown() ? 1 : 0);
}

bool Menu::board_upload_row_shown() const {
  // The upload affordance appears only on a build that can actually pass
  // the worker's attestation requirement — a viewer-only build (no verify
  // backend) never shows a doomed UPLOAD row (LEADERBOARD.md) — and only
  // on the screen of the season the upload would actually land in
  // (best.nrp's own): an older build's best is reachable by flipping the
  // SEASON row to it, instead of uploading invisibly from the live screen.
  // A CONCLUDED upload's status row (placed / failed) survives the socket:
  // a refusal that arrives with (or is followed by) a close must not
  // vanish into the generic UNAVAILABLE footer — the row is the answer
  // the player is reading, and confirm no-ops without a socket anyway.
  return board_best_clean_ && net_board_can_submit() &&
         board_best_season_ == board_season_ &&
         (board_net_ != nullptr || board_up_phase_ >= 2);
}

// Is the local best already one of the visible board rows? (run_id match —
// the same test the " - YOU" tag uses.) When true, the rank-of footer is
// redundant with the tagged row and is suppressed.
bool Menu::board_best_on_board() const {
  if (board_best_run_id_.empty()) return false;
  for (const NetBoard::Row &r : board_rows_)
    if (r.run_id == board_best_run_id_) return true;
  return false;
}

// The UPLOAD row's label when board_up_phase_ == 3 (finished, not placed).
// Distinguishes benign refusals from real failures.
const char *Menu::board_upload_status_text() const {
  const std::string &r = board_up_reason_;
  if (r == "not-best" || r == "already-submitted")
    return "BEST ALREADY ON THE BOARD";
  if (r == "unverified") return "UPLOAD FAILED - NOT VERIFIED";
  // Production admits only canonical SEASON-file seasons; a dev/old build's
  // best belongs to a bucket the board does not take (LEADERBOARD.md).
  if (r == "bad-season") return "SEASON NOT ACCEPTED";
  if (r == "rate-limited") return "UPLOAD FAILED - TRY LATER";
  if (r == "connection") return "UPLOAD FAILED - CONNECTION";
  return "UPLOAD FAILED";
}

void Menu::board_start_upload() {
  if (!board_net_ || board_transfer_busy()) return;
  board_up_retried_ = false;
  board_up_retry_deadline_ = 0;
  const NetIdentity &me = net_local_identity();
  std::string cred = net_board_verify_credential();
  board_up_sent_cred_ = cred;  // for the retry's freshness compare
  board_net_->submit(Replay::best_path(), me.platform, me.name, cred);
  board_up_phase_ = 1;
  SDL_Log("board: uploading best.nrp (menu)");
}

// The socket carries ONE transfer at a time (RtcBoard's single phase_), so
// a fetch and an upload must never overlap — starting one mid-flight would
// strand the other with no terminal event and wedge the screen.
bool Menu::board_transfer_busy() const {
  return board_fetching_ || board_up_phase_ == 1;
}

// Step the browsed season through the worker's list (wrapping). A fresh
// request repaints the rows; the upload row and rank-of footer re-gate on
// the new season by themselves.
void Menu::board_cycle_season(int dir) {
  int n = (int)board_seasons_.size();
  if (n < 2) return;  // nothing to browse yet (or only one season exists)
  int at = 0;
  for (int i = 0; i < n; i++)
    if (board_seasons_[i].season == board_season_) { at = i; break; }
  at = (at + dir + n) % n;
  board_season_ = board_seasons_[at].season;
  board_request();
}

void Menu::board_nav_confirm() {
  if (board_sel_ == 0) {
    board_players_ = board_players_ == 1 ? 2 : 1;
    board_request();
    return;
  }
  if (board_sel_ == 1) {
    board_cycle_season(1);
    return;
  }
  int ri = board_sel_ - 2;
  if (ri < (int)board_rows_.size()) {
    const NetBoard::Row &r = board_rows_[ri];
    // Score-only rows (blob demoted by retention) and rows this build
    // cannot play back (another version's format — the season browser
    // makes those reachable) are unselectable: nothing to watch, and the
    // footer names why. One transfer at a time (guards BOTH a second
    // fetch AND a fetch racing an in-flight upload).
    if (r.has_replay && net_board_replay_watchable(r) &&
        !board_transfer_busy() && board_net_) {
      board_fetching_ = true;
      board_net_->fetch(board_season_, r.run_id, Replay::download_path());
      SDL_Log("board: fetching replay run_id=%s",
              net_board_sanitize(r.run_id, 24).c_str());
    }
    return;
  }
  if (board_upload_row_shown() && !board_transfer_busy() &&
      (board_up_phase_ == 0 || board_up_phase_ == 3))
    board_start_upload();
}

void Menu::board_poll() {
  if (!board_net_) return;
  // A pending auto-retry after an "unverified" upload (see the Error
  // handler): poll peek (no re-mint) until a value different from the
  // rejected one appears, then consume + resubmit it once; give up at the
  // deadline.
  if (board_up_retry_deadline_) {
    std::string peek = net_board_verify_credential_peek();
    if (!peek.empty() && peek != board_up_sent_cred_) {
      board_up_retry_deadline_ = 0;
      const NetIdentity &me = net_local_identity();
      std::string fresh = net_board_verify_credential();  // consume the fresh one
      board_up_sent_cred_ = fresh;
      board_net_->submit(Replay::best_path(), me.platform, me.name, fresh);
      board_up_phase_ = 1;
      SDL_Log("board: retrying upload with a fresh credential (menu)");
    } else if (currentTime >= board_up_retry_deadline_) {
      board_up_retry_deadline_ = 0;
      board_up_phase_ = 3;
      board_up_reason_ = "unverified";
      SDL_Log("board: no fresh credential before deadline (menu)");
    }
  }
  NetBoard::Event ev;
  while (board_net_->poll(ev)) {
    switch (ev.kind) {
      case NetBoard::Event::Top:
        board_rows_ = ev.rows;
        board_loading_ = false;
        if (board_sel_ >= board_entry_count())
          board_sel_ = board_entry_count() - 1;
        board_ensure_visible();
        break;
      case NetBoard::Event::RankOf:
        // Drop an answer for a board we have since flipped away from
        // (players is echoed by the worker).
        if (ev.players == 0 || ev.players == board_players_)
          board_your_rank_ = ev.place;
        break;
      case NetBoard::Event::Seasons:
        // The worker's list is newest-submission-first. Every season key
        // is drawn on the SEASON row, so it passes the socket's security
        // boundary first (net_board.h: worker strings reach the font only
        // sanitized — the socket skips TLS verification). A legit worker's
        // keys pass season_ok at submit, so sanitizing is the identity
        // for real data; anything that folds to nothing is dropped. Keep
        // this build's season at the front even before it has any rows (a
        // fresh season must still be the default screen and reachable by
        // cycling).
        board_seasons_.clear();
        for (const NetBoard::Season &in : ev.seasons) {
          NetBoard::Season s = in;
          s.season = net_board_sanitize(s.season, 22);
          if (!s.season.empty()) board_seasons_.push_back(s);
        }
        {
          // Ensure best.nrp's season stays reachable (its screen carries
          // the UPLOAD row — see open_board's seed) alongside the build's.
          if (!board_best_season_.empty() &&
              board_best_season_ != board_build_season_) {
            bool have_best = false;
            for (const NetBoard::Season &s : board_seasons_)
              if (s.season == board_best_season_) { have_best = true; break; }
            if (!have_best) {
              NetBoard::Season bs;
              bs.season = board_best_season_;
              board_seasons_.push_back(bs);
            }
          }
          bool have_own = false;
          for (const NetBoard::Season &s : board_seasons_)
            if (s.season == board_build_season_) { have_own = true; break; }
          if (!have_own) {
            NetBoard::Season own;
            own.season = board_build_season_;
            board_seasons_.insert(board_seasons_.begin(), own);
          }
        }
        break;
      case NetBoard::Event::Placed:
        board_up_phase_ = 2;
        board_up_rank_ = ev.place;
        SDL_Log("board: placed #%d (menu)", ev.place);
        board_request();  // the new row should appear in the list
        break;
      case NetBoard::Event::FetchDone: {
        board_fetching_ = false;
        // Downloaded replays are transient (download.nrp) and hand off to
        // the ordinary R2 playback path; a parse failure just stays here.
        if (GLGame *g =
                GLGame::start_replay_playback(Replay::download_path())) {
          request_state_change(g);
          return;
        }
        SDL_Log("board: downloaded replay would not play");
        break;
      }
      case NetBoard::Event::Error:
        SDL_Log("board: error %s (menu)",
                net_board_sanitize(ev.reason).c_str());
        if (board_up_phase_ == 1 && !board_up_retried_ &&
            ev.reason == "unverified") {
          // The submit's own credential read already fired the next async
          // mint, so poll peek for a fresh (different) one and retry ONCE.
          // The socket stays open; the poll runs from the top of board_poll.
          board_up_retried_ = true;
          board_up_retry_deadline_ = currentTime + BOARD_UPLOAD_RETRY_TIMEOUT_MS;
          SDL_Log("board: upload unverified - waiting for a fresh credential (menu)");
        } else if (board_up_phase_ == 1) {
          board_up_phase_ = 3;
          board_up_reason_ = net_board_sanitize(ev.reason, 32);
        } else if (board_fetching_) {
          board_fetching_ = false;
        } else {
          board_loading_ = false;
          board_error_ = true;
        }
        break;
      case NetBoard::Event::Closed:
        SDL_Log("board: connection closed (menu)");
        board_loading_ = false;
        board_fetching_ = false;
        board_error_ = true;
        if (board_up_phase_ == 1) {
          board_up_phase_ = 3;
          board_up_reason_ = "connection";
        }
        delete board_net_;
        board_net_ = nullptr;
        return;
      default:
        break;
    }
  }
}

void Menu::adjust_active_row(int delta, bool wrap) {
  const OptRow &r = opt_row(active_row_);
  int *idx, num;
  switch (r.kind) {
    case 0: idx = &sensitivity_index_[r.player]; num = NUM_SENSITIVITY;  break;
    case 1: idx = &smoothing_index_[r.player];   num = NUM_SMOOTHING;    break;
    case 2: idx = &camera_index_[r.player];      num = NUM_CAMERA;       break;
    case 3: idx = &star_density_index_;          num = NUM_STAR_DENSITY; break;
    case 5: idx = &leaderboard_index_;           num = NUM_RECORD;       break;
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
  g_prefs.leaderboard_prompts          = (leaderboard_index_ == 1);
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
