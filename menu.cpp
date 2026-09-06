#include "typer.h"
#include "asset_path.h"
#include "audio_volume.h"
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
#include "pad.h"
#include "net_transport.h"
#include "preferences.h"
#include "touch_controls.h"
#include "presence.h"
#include "replay.h"
#include "stats.h"
#include "gl_compat.h"
#include "mat4.h"
#include "steam_build.h"
#include "view/overlay.h"
#include "view/tap_band.h"
#include <ctime>
#include <iostream>
#include <string>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdio>
// web_main.cpp: one-shot, true once a /play/?replay= watch deep link has
// staged download.nrp for playback (polled below in Menu::tick).
bool web_take_replay_watch();
#endif

static const float SENSITIVITY_VALUES[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
static const char* SENSITIVITY_LABELS[] = {"SLOW", "LOW", "NORMAL", "HIGH", "MAX"};
static const int NUM_SENSITIVITY = 5;

static const float SMOOTHING_VALUES[] = {0.0f, 0.004f, 0.006f, 0.008f, 0.010f};
static const char* SMOOTHING_LABELS[] = {"OFF", "NORMAL", "HIGH", "HIGHER", "MAX"};
static const int NUM_SMOOTHING = 5;

static const float STAR_DENSITY_MULTIPLIERS[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
static const char* STAR_DENSITY_LABELS[] = {"MINIMAL", "SPARSE", "MEDIUM", "MANY", "FULL"};
static const int NUM_STAR_DENSITY = 5;

// The AUDIO sub-menu's volume steps (Preferences::master_volume /
// music_volume, pushed onto the mixer by AudioVolume::apply). Step 1 is a
// true OFF — 0 silences outright, it does not merely duck.
static const float VOLUME_VALUES[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
static const char* VOLUME_LABELS[] = {"OFF", "QUIET", "HALF", "LOUD", "FULL"};
static const int NUM_VOLUME = 5;

// Per-player camera: index 0 = FIXED (view locked to the world), 1 = ROTATE
// (view follows the ship's heading). Stored as PlayerKeys::rotate_view.
static const char* CAMERA_LABELS[] = {"FIXED", "ROTATE"};
static const int NUM_CAMERA = 2;

// The CAMERA sub-menu's zoom rows. camera_zoom scales the visible span in
// tan-space over the classic 85-degree view (NORMAL is exactly that view);
// the range is deliberately modest — WIDEST already spans past the gen-0
// wrap on wide monitors, and quantum observation stays pinned to the
// classic view either way (GLGame::is_point_faced_by_any_player). The
// table itself lives in preferences.h: the in-game touch zoom zones step
// the same five values.
static const float *const ZOOM_VALUES = CAMERA_ZOOM_VALUES;
static const char *const *const ZOOM_LABELS = CAMERA_ZOOM_LABELS;
static const int NUM_ZOOM = CAMERA_ZOOM_STEPS;

// Speed-follow zoom (PlayerKeys::speed_zoom): the view widens with the
// ship's speed on top of the zoom row, eased on the sim clock
// (GLShip::smooth_camera). SUBTLE is the default.
static const float SPEED_ZOOM_VALUES[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
static const char* SPEED_ZOOM_LABELS[] = {"OFF", "SUBTLE", "NORMAL", "HIGH", "MAX"};
static const int NUM_SPEED_ZOOM = 5;

// Auto-record replays (REPLAY.md): 0 = OFF, 1 = ON. Default is now ON for
// fresh installs (the low-end field pass cleared the recorder on real
// hardware, 2026-07-28), so this row is mostly an opt-OUT — and the reason
// it has to exist at all: NEWTONIA_REPLAY_ENABLE is lost on any launch the
// player does not control (an invite starts a fresh process with no intent
// extras), and hand-editing preferences.ini is not a player-facing answer.
static const char* RECORD_LABELS[] = {"OFF", "ON"};
static const int NUM_RECORD = 2;
// Touch input method (Preferences::touch_one_hand): the classic two-hand
// layout (left-half joystick + shoot/mine/boost buttons) vs the one-hand
// whole-screen stick (tap fires the primary, a long press the secondary —
// touch_controls.h). Touch row list only: desktop/controller input has no
// OSD to re-arrange, and the desktop list sits at its row budget.
static const char* INPUT_LABELS[] = {"TWO HANDS", "ONE HAND"};
static const int NUM_INPUT = 2;
// Handedness (Preferences::touch_handedness), touch list only. One hand:
// LEFT/RIGHT park the stick's resting ring where that thumb sits, CENTRE
// keeps it centred. LEFT also MIRRORS the inputs (touch_layout_mirrored):
// pause + zoom column in one hand, the whole layout in two hands — stick
// right, shoot/mine/boost left. CENTRE is the shipped default; RIGHT and
// CENTRE are both the classic arrangement in two-hand mode.
static const char* HANDEDNESS_LABELS[] = {"LEFT", "CENTRE", "RIGHT"};
static const int NUM_HANDEDNESS = 3;
// leaderboard_prompts: ON = ask at game over (the per-run opt-out), OFF =
// upload a qualifying best automatically, still showing the card's
// UPLOADING/UPLOADED status text (decided with Glenn 2026-08-03). Labelled
// ASK/AUTO so "off" can't be read as "uploads off" — a missed prompt is
// what surfaced the old never-upload reading in the field.
static const char* LEADERBOARD_LABELS[] = {"AUTO", "ASK"};
static const int NUM_LEADERBOARD = 2;

// The Options screen rows, in display order. kind: 0=sensitivity, 1=smoothing,
// 2=camera fixed/rotate, 3=star density, 4=auto-record replays,
// 5=leaderboard prompts, 6=master volume, 7=music volume (the two AUDIO
// sub-menu rows), 8=the AUDIO row itself — a sub-menu OPENER: it cycles
// nothing, draws no steps, and confirm/tap/left/right all open the audio
// screen; 9=the CAMERA row, the second opener (its sub-screen holds the
// per-player smoothing/rotation/zoom rows — four rows per player would
// blow the flat list's budget, the same reason AUDIO went a level down);
// 10=zoom, 11=speed-follow zoom (CAMERA sub-menu rows); 12=touch input
// method, 13=one-hand handedness (touch list only — see INPUT_LABELS /
// HANDEDNESS_LABELS above). P2 rows are
// desktop-only — mobile (touch) shows Player 1 plus the shared options.
// Options is desktop/controller-only today (see Menu::show_options_row),
// so the touch list is future-proofing.
namespace { struct OptRow { int kind; int player; const char *name; }; }
static const OptRow OPT_ROWS_DESKTOP[] = {
  {0, 0, "P1  SENSITIVITY"},
  {0, 1, "P2  SENSITIVITY"},
  {0, 2, "P3  SENSITIVITY"},
  {0, 3, "P4  SENSITIVITY"},
  {9, 0, "CAMERA"},
  {3, 0, "STAR  DENSITY"},
  {8, 0, "AUDIO"},
  {4, 0, "RECORD  REPLAYS"},
  // Must stay LAST: opt_row_count drops it on builds with no leaderboard.
  {5, 0, "LEADERBOARD  UPLOAD"},
};
// Mobile shows Player 1 + shared options only, so the "P1" prefix is dropped.
static const OptRow OPT_ROWS_TOUCH[] = {
  {0, 0, "SENSITIVITY"},
  {12, 0, "INPUT METHOD"},
  {13, 0, "HANDEDNESS"},
  {9, 0, "CAMERA"},
  {3, 0, "STAR DENSITY"},
  {8, 0, "AUDIO"},
  {4, 0, "RECORD REPLAYS"},
  {5, 0, "LEADERBOARD UPLOAD"},  // LAST — see the desktop table
};
// The CAMERA sub-screen's rows: everything about what the camera shows,
// per player — follow smoothing, fixed/rotate (named ROTATION here; under
// a CAMERA heading a row also called CAMERA read as a stutter), base zoom
// and the speed-follow zoom amount. 16 desktop rows — the flat list's
// proven budget (the pre-sub-menu options list held exactly 16).
static const OptRow OPT_ROWS_CAMERA_DESKTOP[] = {
  {1, 0, "P1  SMOOTHING"}, {2, 0, "P1  ROTATION"}, {10, 0, "P1  ZOOM"}, {11, 0, "P1  SPEED  ZOOM"},
  {1, 1, "P2  SMOOTHING"}, {2, 1, "P2  ROTATION"}, {10, 1, "P2  ZOOM"}, {11, 1, "P2  SPEED  ZOOM"},
  {1, 2, "P3  SMOOTHING"}, {2, 2, "P3  ROTATION"}, {10, 2, "P3  ZOOM"}, {11, 2, "P3  SPEED  ZOOM"},
  {1, 3, "P4  SMOOTHING"}, {2, 3, "P4  ROTATION"}, {10, 3, "P4  ZOOM"}, {11, 3, "P4  SPEED  ZOOM"},
};
static const OptRow OPT_ROWS_CAMERA_TOUCH[] = {
  {1, 0, "SMOOTHING"},
  {2, 0, "ROTATION"},
  {10, 0, "ZOOM"},
  {11, 0, "SPEED ZOOM"},
};
static int camera_row_count() {
  return is_touch_mode()
             ? (int)(sizeof(OPT_ROWS_CAMERA_TOUCH) / sizeof(OPT_ROWS_CAMERA_TOUCH[0]))
             : (int)(sizeof(OPT_ROWS_CAMERA_DESKTOP) / sizeof(OPT_ROWS_CAMERA_DESKTOP[0]));
}
static const OptRow &camera_row(int r) {
  return is_touch_mode() ? OPT_ROWS_CAMERA_TOUCH[r] : OPT_ROWS_CAMERA_DESKTOP[r];
}
// The AUDIO sub-screen's rows, drawn by the same row loop the options list
// uses (same columns, same band geometry — a sub-screen that hand-rolled
// its own layout is exactly the drift menu_select.h exists to prevent).
static const OptRow OPT_ROWS_AUDIO_DESKTOP[] = {
  {6, 0, "MASTER  VOLUME"},
  {7, 0, "MUSIC  VOLUME"},
};
static const OptRow OPT_ROWS_AUDIO_TOUCH[] = {
  {6, 0, "MASTER VOLUME"},
  {7, 0, "MUSIC VOLUME"},
};
static int audio_row_count() {
  return (int)(sizeof(OPT_ROWS_AUDIO_DESKTOP) / sizeof(OPT_ROWS_AUDIO_DESKTOP[0]));
}
static const OptRow &audio_row(int r) {
  return is_touch_mode() ? OPT_ROWS_AUDIO_TOUCH[r] : OPT_ROWS_AUDIO_DESKTOP[r];
}
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

// ---- main-menu vertical anchors ----
// All hang off an effective half-height: landscape aspect pins
// Typer::scaled_window_height at 600, so these reduce to the classic
// fixed layout (title 410, rows 200..-300/-280, high score -330/-300,
// copyright -420). Portrait GROWS the real half-height (600/aspect);
// taking HALF that surplus spreads the menu into the extra vertical
// real estate without exploding it to the screen edges — full-surplus
// anchoring was field-tested and rejected as too sparse (2026-08-03).
// Read live — scaled_window_height changes on resize.
static int menu_half_height() {
  return 600 + ((int)Typer::scaled_window_height - 600) / 2;
}
static int menu_title_y()     { return menu_half_height() - 190; }
// Breathing room between the high-score / copyright cluster's lines in
// portrait (0 in landscape — the classic layout keeps its tight stack).
static int menu_cluster_pad() { return (menu_half_height() - 600) / 12; }
// Top of the menu-row band: below the title (size 80 descends 160) with
// deliberate air under it.
static int menu_rows_top()    { return menu_title_y() - 210; }
// Bottom of the menu-row band: desktop packs rows above the high-score
// block; touch spreads them over a taller band (they're finger targets)
// and the score block moves down to make room.
static int menu_rows_bottom() {
  return is_touch_mode() ? -(menu_half_height() - 300)
                         : -(menu_half_height() - 320);
}
static int menu_high_score_y() {
  return menu_rows_bottom() - (is_touch_mode() ? 30 : 20) - menu_cluster_pad();
}
static int menu_high_score_num_y() {
  return menu_high_score_y() - 40 - menu_cluster_pad();
}
// Below the score number when the cluster is present (landscape gaps: 50
// touch / 80 desktop reproduce the classic -420); the fixed near-bottom
// anchor otherwise.
static int menu_copyright_y(bool has_high_score) {
  if (has_high_score)
    return menu_high_score_num_y() - (is_touch_mode() ? 50 : 80) -
           menu_cluster_pad();
  return -(menu_half_height() - 180);
}

// ---- sub-screen (options/replays/board) portrait anchors ----
// Same rule as the main menu: everything hangs off menu_half_height(),
// reducing to the classic fixed layout in landscape and spreading over
// half the portrait surplus otherwise (field request 2026-08-03).
static int menu_screen_heading_y() {
  return menu_half_height() - (is_touch_mode() ? 260 : 232);
}
// The RETURN/BACK TO MENU band, re-anchored the same half-surplus down
// (0 in landscape). ONE definition feeds the three screens' draw AND
// hit-test (the TapBand rule).
static float menu_band_lift() { return (float)(600 - menu_half_height()); }
static TapBand menu_exit_band() {
  return TapBand::return_to_menu.lifted(menu_band_lift());
}
// The same band for the hit-test: off touch a mouse gets the glyph box
// rather than the finger-sized strip that runs to the screen edge (see
// TapBand::for_pointer — the lobby's twin of this ate clicks well below
// the text). The DRAW keeps the band above, so the label never moves.
static TapBand menu_exit_hit() { return menu_exit_band().for_pointer(); }

// Options/replays row-band geometry — shared by the draw and the tap
// hit-test so a tap always lands on the row it appears on. Touch rows fill
// the band above the EXIT TO MENU strip; desktop rows run deeper (no exit
// strip below) — and desktop taps are real input too: the Steam Deck's
// touchscreen reaches the desktop build as pointer clicks (glut.cpp
// forwards them as taps), so the desktop layout needs the same
// draw/hit-test pairing the touch layout has. Landscape values: touch
// 250..-210, desktop 250..-300.
static float touch_opt_top()    { return (float)(menu_half_height() - 350); }
static float touch_opt_bottom() { return -(float)(menu_half_height() - 390); }
static float desk_opt_top()     { return (float)(menu_half_height() - 350); }
static float desk_opt_bottom()  { return -(float)(menu_half_height() - 300); }
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
// Board slots (Typer virtual units), hanging off the same portrait-aware
// half-height: the control pair sits tight under the title, the score
// table runs at constant pitch under its headers, and the UPLOAD action +
// footer hold the bottom. Landscape values: 268/214/158/116, pitch 48,
// -262/-300/-364. Touch rows draw over TWO lines (name/score above,
// platform badge/level/date below — portrait has no width for the
// desktop's six columns), so their pitch is taller.
// The BOARD toggle and SEASON browser are primary tap targets: on touch
// they sit a full hit zone apart (104 > the ±50 control zones in
// board_entry_at — at the classic 54 the zones overlapped and every tap
// between them landed on BOARD, the first entry tested). Desktop keeps
// the classic tight stack; the header/table slots cascade below.
static int board_y_toggle() {
  return menu_half_height() - (is_touch_mode() ? 348 : 332);
}
static int board_y_season() {
  return board_y_toggle() - (is_touch_mode() ? 104 : 54);
}
static int board_y_header() {
  return board_y_season() - (is_touch_mode() ? 72 : 56);
}
static int board_y_row0()   { return board_y_header() - 42; }
static int board_row_pitch() { return is_touch_mode() ? 150 : 48; }
static int board_y_range()  { return -(menu_half_height() - 338); }
static int board_y_upload() { return -(menu_half_height() - 300); }
static int board_y_footer() { return -(menu_half_height() - 236); }
// How many rows the table window shows at once: as many as fit between
// the first row slot and the range indicator (8 on landscape desktop —
// the classic layout; portrait's taller band fits more). The fetch asks
// for the full top-100 board and the window scrolls over it (the range
// indicator is the "N-M OF R" line under the table, tappable on touch:
// left half pages up, right half down).
static int board_visible_rows() {
  return (board_y_row0() - board_y_range() - 26) / board_row_pitch() + 1;
}
// Sentinel for a row entry scrolled out of the window (never drawn,
// never hit-tested — no real slot is anywhere near it).
static const int BOARD_Y_OFFSCREEN = 10000;

static int touch_opt_center(int i, int n) {
  return opt_row_center(i, n, touch_opt_top(), touch_opt_bottom());
}
static int touch_opt_row_at(float ny, int n) {
  return opt_row_at(ny, n, touch_opt_top(), touch_opt_bottom());
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

// Nearest step to a stored pref value, for every stepped row (hand-edited
// INIs land on the closest step). One loop over the row's table — this
// used to be a hand-rolled copy per table, five by the time the zoom rows
// arrived, differing only in a fallback index no finite value ever reaches.
static int nearest_value_index(float value, const float *values, int n) {
  int best = 0;
  float best_dist = 1e6f;
  for (int i = 0; i < n; i++) {
    float d = value > values[i] ? value - values[i] : values[i] - value;
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
  // camera_z 0: the menu's starfield camera sits at the origin (see draw()),
  // not the game's z=1000 — the star quads size themselves by camera distance.
  starfield(new GLStarfield(Point(default_world_width, default_world_height), star_density_scale(), 0.0f)) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    sensitivity_index_[i] = nearest_value_index(g_prefs.player_keys[i].keyboard_sensitivity,
                                                SENSITIVITY_VALUES, NUM_SENSITIVITY);
    smoothing_index_[i]   = nearest_value_index(g_prefs.player_keys[i].camera_smoothing,
                                                SMOOTHING_VALUES, NUM_SMOOTHING);
    camera_index_[i]      = g_prefs.player_keys[i].rotate_view ? 1 : 0;
    zoom_index_[i]        = nearest_value_index(g_prefs.player_keys[i].camera_zoom,
                                                ZOOM_VALUES, NUM_ZOOM);
    speed_zoom_index_[i]  = nearest_value_index(g_prefs.player_keys[i].speed_zoom,
                                                SPEED_ZOOM_VALUES, NUM_SPEED_ZOOM);
  }
  star_density_index_   = nearest_value_index(g_prefs.star_density,
                                              STAR_DENSITY_MULTIPLIERS, NUM_STAR_DENSITY);
  auto_record_index_    = g_prefs.auto_record_replays ? 1 : 0;
  leaderboard_index_    = g_prefs.leaderboard_prompts ? 1 : 0;
  input_index_          = g_prefs.touch_one_hand ? 1 : 0;
  handedness_index_     = g_prefs.touch_handedness;
  master_volume_index_  = nearest_value_index(g_prefs.master_volume, VOLUME_VALUES, NUM_VOLUME);
  music_volume_index_   = nearest_value_index(g_prefs.music_volume, VOLUME_VALUES, NUM_VOLUME);
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
  // Re-push the stored volume levels now that audio is certainly OPEN:
  // Mix_OpenAudio RESETS the music volume to full (measured on 2.8 — the
  // master level survives, the music level does not), and the platform
  // entry points call load_preferences() before they open audio, so the
  // apply() riding the load was silently wiped — the field symptom was
  // settings that showed in the menu but never sounded on launch. Every
  // startup path passes through this constructor after audio init, so
  // this is the one platform-neutral point that is always late enough.
  AudioVolume::apply();
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
    Typer::draw_centered(0, menu_screen_heading_y(), "LEADERBOARD",
                         touch ? 30 : 26);
    // Entries: [0] the SOLO/CO-OP toggle, [1] the SEASON browser, [2..]
    // the score rows, then the UPLOAD BEST RUN action (the game-over
    // prompt's retry path). Same shared row geometry as the
    // options/replays screens (TapBand rule).
    int n = board_entry_count();
    // Column layout: Typer's advance is 2x the size, so with the row
    // spanning the cursors (-623..609) the name column must absorb the
    // WORST case — a 24-glyph attested name + tick + " - YOU" — without
    // running into the badge (field, 2026-08-03: a 16-glyph Android name
    // mashed straight into ANDROID). Name renders at size 10 and is
    // TRUNCATED to the 25-glyph budget (suffixes charged first), ending
    // by x=45; then badge (9: 60..186), score (12: 200..392, 8 digits),
    // bare level (10: 400..460) and the yy-mm-dd date (8: 476..604) or
    // right-flush NO REPLAY / OTHER VER tag (8: 460..604).
    const int CURSOR_L = -623, CURSOR_R = 609;
    // Touch reads at arm's length on a phone, so its two-line entries use
    // larger glyphs than the desktop's six-column table — with their own
    // columns sized for them (field-iterated 2026-08-03: sized up twice;
    // ~10 big entries per portrait page beat 14 small ones). Touch
    // line 1: rank (16: 4 glyphs from -640), name (16, a 22-glyph budget
    // ending by x=214), score (18: 260..548, 8 digits). Line 2: badge
    // (13: -490..), "LVL n" (13: 260..), date/tag (12: 500..716 worst
    // case, inside portrait's ±800). Desktop keeps the classic sizes and
    // columns.
    const int RANK_X  = touch ? -640 : -560;
    const int NAME_X  = touch ? -490 : -455;
    const int SCORE_X = touch ? 260 : 200;
    const int BADGE_X = 60, LEVEL_X = 400, DATE_X = 476, TAG_X = 460;
    const int TAG2_X = 500;  // touch line 2's date/tag column
    const int NAME_GLYPHS = touch ? 22 : 25;  // name-column glyph budget
    const int SZ_RANK = touch ? 16 : 11, SZ_NAME = touch ? 16 : 10,
              SZ_SCORE = touch ? 18 : 12, SZ_HDR = touch ? 13 : 9,
              SZ_SUB = touch ? 13 : 9, SZ_TAG = touch ? 12 : 8;
    // Column headers over the score table (structure the bare values lost
    // when the worded labels were dropped for column fit).
    if (!board_rows_.empty()) {
      Typer::draw(RANK_X, board_y_header(), "#", SZ_HDR);
      Typer::draw(NAME_X, board_y_header(), "PLAYER", SZ_HDR);
      Typer::draw(SCORE_X, board_y_header(), "SCORE", SZ_HDR);
      // Touch rows carry "LVL n" and the date inline on their second
      // line, so only the desktop's columns get headers for them.
      if (!touch) {
        Typer::draw(LEVEL_X, board_y_header(), "LVL", 9);  // LEVEL overran DATE
        Typer::draw(DATE_X, board_y_header(), "DATE", 9);
      }
    }
    for (int e = 0; e < n; e++) {
      char text[96];
      if (e == 0) {
        snprintf(text, sizeof(text), "BOARD: %s",
                 board_players_ == 1 ? "SOLO" : "CO-OP");
      } else if (e == 1) {
        // The browsed season. A non-canonical season is a dev/git-describe
        // bucket (locally seeded — production never lists or admits one);
        // tag it DEV so it can't read as live production data. Canonical:
        // tag this build's own ("LIVE") when the browser has somewhere
        // else to go.
        bool own = board_season_ == board_build_season_;
        const char *tag = "";
        if (!net_board_season_canonical(board_season_))
          tag = " - DEV";
        else if (own && board_seasons_.size() > 1)
          tag = " - LIVE";
        snprintf(text, sizeof(text), "SEASON: %s%s", board_season_.c_str(),
                 tag);
      }
      int y = board_entry_y(e);
      if (y == BOARD_Y_OFFSCREEN) continue;  // scrolled out of the window
      if (!touch)
        MenuSelect::draw_row_cursor(board_sel_ == e, CURSOR_L, CURSOR_R, y, 13);
      if (e <= 1) {
        Typer::draw_centered(0, y, text, touch ? 18 : 14);
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
        bool attested_name = r.verified && !name.empty();
        if (name.empty()) name = "PLAYER";
        // Fit the glyph budget with the suffixes charged first: the tick
        // and " - YOU" must always survive, so the NAME truncates.
        bool you = !board_best_run_id_.empty() && r.run_id == board_best_run_id_;
        size_t cap = (size_t)NAME_GLYPHS - (attested_name ? 1 : 0) -
                     (you ? 6 : 0);
        if (name.size() > cap) name.resize(cap);
        // Platform-attested names carry the verified tick inline (same
        // forgery-proof glyph as the netplay identity UI — the char is
        // stripped from every wire name, so only this append can draw
        // it). iOS aliases render bare: the visible difference between a
        // platform-vouched name and a claimed one. The platform BADGE
        // needs no tick — admission requires attestation, so the badge's
        // presence already is the guarantee.
        if (attested_name) name += Typer::VERIFIED_TICK;
        if (you) name += " - YOU";
        char rank_buf[12], score_buf[24], level_buf[16];
        snprintf(rank_buf, sizeof(rank_buf), "#%d", r.rank);
        snprintf(score_buf, sizeof(score_buf), "%u", r.score);
        snprintf(level_buf, sizeof(level_buf), "%u", r.generation + 1);
        // The last column's content: the date, unless the replay cannot
        // be watched — retention-demoted (gone) or another version's
        // format (the season browser reaches those) — which matters more
        // than when.
        const char *tag = NULL;
        char date_buf[16] = "";
        if (!r.has_replay) {
          tag = "NO REPLAY";
        } else if (!net_board_replay_watchable(r)) {
          tag = "OTHER VER";
        } else if (r.date > 0) {
          time_t t = (time_t)(r.date / 1000);  // submitted_at is epoch ms
          struct tm *tmv = localtime(&t);
          if (tmv) strftime(date_buf, sizeof(date_buf), "%y-%m-%d", tmv);
        }
        // Platform badge (STEAM/WEB/IOS/ANDROID) so rows from different
        // platforms — and the verified vs claimed distinction above — are
        // visible, not collapsed into an anonymous name.
        const char *badge = net_platform_label(r.platform);
        Typer::draw(RANK_X, y, rank_buf, SZ_RANK);
        Typer::draw(NAME_X, y, name.c_str(), SZ_NAME);
        Typer::draw(SCORE_X, y, score_buf, SZ_SCORE);
        if (touch) {
          // Second line: the columns portrait has no width for — the
          // platform badge under the name, "LVL n" under the score, and
          // the date / unwatchable tag on the right. Offset chosen so the
          // within-entry gap (52) stays clearly tighter than the gap to
          // the next entry (98 at the 150 pitch) — equal gaps made the
          // pairing ambiguous — while the two lines keep a little air
          // between themselves too (field-iterated 2026-08-03).
          int y2 = y - 52;
          if (badge && badge[0]) Typer::draw(NAME_X, y2, badge, SZ_SUB);
          char lvl2[24];
          snprintf(lvl2, sizeof(lvl2), "LVL %s", level_buf);
          Typer::draw(SCORE_X, y2, lvl2, SZ_SUB);
          if (tag)
            Typer::draw(TAG2_X, y2, tag, SZ_TAG);
          else if (date_buf[0])
            Typer::draw(TAG2_X, y2, date_buf, SZ_TAG);
        } else {
          if (badge && badge[0]) Typer::draw(BADGE_X, y, badge, 9);
          Typer::draw(LEVEL_X, y, level_buf, 10);
          if (tag)
            Typer::draw(TAG_X, y, tag, 8);
          else if (date_buf[0])
            Typer::draw(DATE_X, y, date_buf, 8);
        }
        continue;
      }
      // The UPLOAD BEST RUN action row, phase-labelled (through the
      // browsed board's eyes — a foreign board's upload reads as idle).
      switch (board_up_phase_shown()) {
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
      Typer::draw_centered(0, y, text, touch ? 18 : 14);
    }
    // Scroll range under the table when the board exceeds the window —
    // both the "there is more" affordance and, on touch, the pager (tap
    // left half = up, right half = down; see touch_tap).
    if ((int)board_rows_.size() > board_visible_rows()) {
      char range[40];
      int last = board_scroll_ + board_visible_rows();
      if (last > (int)board_rows_.size()) last = (int)board_rows_.size();
      snprintf(range, sizeof(range), "%d-%d OF %d", board_scroll_ + 1, last,
               (int)board_rows_.size());
      int rsz = touch ? 13 : 9;
      Typer::draw_centered(0, board_y_range(), range, rsz);
      // Paging cues (field request 2026-08-03): the line already pages on
      // tap — left half back, right half forward — but nothing said so.
      // Flank it with an arrow on each side that HAS a further page, one
      // glyph-advance of air off the centered text (advance = 2*size, so
      // the text's half-width is strlen*size).
      int half_w = (int)strlen(range) * rsz;
      if (board_scroll_ > 0)
        Typer::draw(-half_w - 3 * rsz, board_y_range(), "<", rsz);
      if (last < (int)board_rows_.size())
        Typer::draw(half_w + rsz, board_y_range(), ">", rsz);
    }
    // Status / footer line, anchored under the UPLOAD slot.
    const int fsz = touch ? 18 : 14;  // footer text follows the touch bump
    int fy = board_y_footer();
    int sel_ri = board_sel_ - 2;  // selected score row, or out of range
    const NetBoard::Row *sel_row =
        (sel_ri >= 0 && sel_ri < (int)board_rows_.size())
            ? &board_rows_[sel_ri] : nullptr;
    if (board_fetching_) {
      // Replay download in flight: the confirm looked dead until playback
      // opened (field, 2026-08-03) — show live progress like the upload
      // row does (transfer_pct is the download's once fetch-ok arrives).
      int pct = board_net_ ? board_net_->transfer_pct() : -1;
      char dl[32];
      if (pct >= 0)
        snprintf(dl, sizeof(dl), "DOWNLOADING %d%%", pct);
      else
        snprintf(dl, sizeof(dl), "DOWNLOADING");
      Typer::draw_centered(0, fy, dl, fsz);
    } else if (board_error_) {
      Typer::draw_centered(0, fy, "LEADERBOARD UNAVAILABLE", fsz);
    } else if (board_loading_) {
      if ((currentTime / 500) % 2)
        Typer::draw_centered(0, fy, "LOADING", fsz);
    } else if (board_rows_.empty()) {
      Typer::draw_centered(0, fy, "NO SCORES THIS SEASON", fsz);
    } else if (sel_row && !sel_row->has_replay) {
      // Why confirming the highlighted row does nothing (mirrors the
      // row's last-column tag; touch has no cursor, so also its only
      // signal after a dead tap).
      Typer::draw_centered(0, fy, "REPLAY NO LONGER STORED", fsz);
    } else if (sel_row && !net_board_replay_watchable(*sel_row)) {
      Typer::draw_centered(0, fy, "REPLAY FROM ANOTHER VERSION", fsz);
    } else if (!Replay::recording_enabled() && net_board_can_submit()) {
      // With recording off there is nothing to submit, so the game-over
      // prompt and the UPLOAD row silently never appear (LEADERBOARD.md
      // L7) — say why, ahead of the rank line: the standing config
      // problem outranks the standing rank. In the lowercase hint
      // register, deliberately quieter than the footer's status texts —
      // an aside, not a status — and 38 glyphs x 10 = 380 fits the
      // portrait 400-unit half-width envelope (count-the-glyphs rule).
      Typer::draw_centered(0, fy, "turn on record replays to enter scores",
                           touch ? 10 : 8);
    } else if (board_your_rank_ > 0 && !board_best_on_board()) {
      // The player's standing whenever their best is NOT already one of
      // the visible rows (rank-of is a projection of the un-uploaded best,
      // so a rank inside the visible range does NOT imply an on-board row
      // — only an actual " - YOU" match does). Shown for both off-board
      // AND would-place-but-not-yet-uploaded bests.
      char yours[32];
      snprintf(yours, sizeof(yours), "YOUR BEST: #%d", board_your_rank_);
      Typer::draw_centered(0, fy, yours, fsz);
    }
    // The exit band is also the last selectable entry (w/s reach it, confirm
    // closes; Esc still exits from anywhere) — the cursor marks show when
    // the selection is on it. Touch draws it always-selected: the band IS
    // the button there. No key prefix in the label: Esc is hard-coded in
    // MenuSelect::is_back, not a rebindable binding, and the row reads as
    // an action now, not a hint.
    menu_exit_band().draw(
        touch ? Typer::cursored("EXIT TO MENU", true).c_str()
              : Typer::cursored("BACK TO MENU",
                                board_sel_ == board_entry_count() - 1)
                    .c_str(),
        currentTime);
  } else if (replays_mode_) {
    bool touch = is_touch_mode();
    Typer::draw_centered(0, menu_screen_heading_y(), "REPLAYS",
                         touch ? 30 : 26);
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
      int y = opt_row_center(i, n, desk_opt_top(), desk_opt_bottom());
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
    // Bottom exit band on BOTH layouts (the lobby's convention): the
    // desktop band stays tappable — the Steam Deck runs the desktop
    // layout and touch needs a way out (field report 2026-07-25).
    // Touch: the band IS the button, so it carries the menu cursor like a
    // selected row. Desktop: the band doubles as the list's last selectable
    // row (index == replay_rows_.size()), so keyboard/controller can walk
    // onto it and confirm out; Esc still exits from anywhere.
    menu_exit_band().draw(
        touch ? Typer::cursored("EXIT TO MENU", true).c_str()
              : Typer::cursored("BACK TO MENU",
                                replay_sel_ == (int)replay_rows_.size())
                    .c_str(),
        currentTime);
  } else if (options_mode_) {
    bool touch = is_touch_mode();
    // The AUDIO and CAMERA sub-screens are the same row machinery over
    // their own tables — same columns, same band, same tap zones; only the
    // heading, the row source and where BACK leads differ.
    Typer::draw_centered(0, menu_screen_heading_y(),
                         audio_mode_ ? "AUDIO"
                                     : camera_mode_ ? "CAMERA" : "OPTIONS",
                         touch ? 30 : 26);

    int n = audio_mode_ ? audio_row_count()
          : camera_mode_ ? camera_row_count() : opt_row_count();
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
    // The NAME column is the tight one, and it is tight in a way no window
    // size reveals: these are fixed constants, so the gap is identical at
    // every resolution and aspect. Typer's advance is 2x the size, so a
    // name of N glyphs at size 12 ends at NAME_X + 24N, and it must clear
    // the leftmost step ink — which depends on the row's STEP COUNT, the
    // part that is easy to get wrong:
    //   2 steps (ON/OFF, FIXED/ROTATE): digits at 70 and 120, so with the
    //     first selected the '[' sits at 70 - 1.5*13 = 50. Room for 19
    //     glyphs, and "LEADERBOARD  UPLOAD" is exactly 19 — 16 units of
    //     daylight, two thirds of a cell.
    //   5 steps (every sensitivity / smoothing / density row): the span is
    //     centred on STEP_CX, so the first digit is at 95 - 2*50 = -5 and
    //     its bracket at -24. Room for only 16 glyphs. The longest today
    //     is "P1  SENSITIVITY" at 15.
    // So: count the glyphs, multiply by 24, add -422, and check the result
    // against -24 for a 5-step row or 50 for a 2-step one, whenever a row
    // is added or renamed. Same discipline as the touch note below.
    const int CURSOR_L = -470, NAME_X = -422, STEP_CX = 95, STEP_GAP = 50,
              VALUE_X = 265, CURSOR_R = 457;

    for (int row = 0; row < n; row++) {
      const OptRow &r = audio_mode_ ? audio_row(row)
                      : camera_mode_ ? camera_row(row) : opt_row(row);

      int num_steps, cur_idx;
      int rec_override = -1;  // >=0 on the RECORD REPLAYS row when forced
      bool opener = false;    // the AUDIO/CAMERA rows: no steps, no value — they open
      const char* const *lbl;
      switch (r.kind) {
        case 0: num_steps = NUM_SENSITIVITY;  cur_idx = sensitivity_index_[r.player]; lbl = SENSITIVITY_LABELS;   break;
        case 1: num_steps = NUM_SMOOTHING;    cur_idx = smoothing_index_[r.player];   lbl = SMOOTHING_LABELS;     break;
        case 2: num_steps = NUM_CAMERA;       cur_idx = camera_index_[r.player];      lbl = CAMERA_LABELS;        break;
        case 3: num_steps = NUM_STAR_DENSITY; cur_idx = star_density_index_;          lbl = STAR_DENSITY_LABELS;  break;
        case 5: num_steps = NUM_LEADERBOARD;  cur_idx = leaderboard_index_;           lbl = LEADERBOARD_LABELS;   break;
        case 6: num_steps = NUM_VOLUME;       cur_idx = master_volume_index_;         lbl = VOLUME_LABELS;        break;
        case 7: num_steps = NUM_VOLUME;       cur_idx = music_volume_index_;          lbl = VOLUME_LABELS;        break;
        case 8: case 9: num_steps = 0; cur_idx = 0; lbl = NULL; opener = true; break;
        case 10: num_steps = NUM_ZOOM;        cur_idx = zoom_index_[r.player];        lbl = ZOOM_LABELS;          break;
        case 11: num_steps = NUM_SPEED_ZOOM;  cur_idx = speed_zoom_index_[r.player];  lbl = SPEED_ZOOM_LABELS;    break;
        case 12: num_steps = NUM_INPUT;       cur_idx = input_index_;                 lbl = INPUT_LABELS;         break;
        case 13: num_steps = NUM_HANDEDNESS;  cur_idx = handedness_index_;            lbl = HANDEDNESS_LABELS;    break;
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
        if (opener) {
          // Sub-menu opener: name + an ellipsis where a value would sit,
          // the row-list idiom for "this opens a screen".
          Typer::draw(-360, cy, r.name, 13);
          Typer::draw_centered(320, cy, "...", 18);
          continue;
        }
        // Name left, value right. Name font sized so the LONGEST label fits
        // clear of the value column: Typer's advance is 2x the size, so at
        // size 13 "LEADERBOARD PROMPT" (18 glyphs) spans -360..+108, clear
        // of the value centred at 320 (field, 2026-08-03: it ran straight
        // over ON/OFF at size 16 from -315). Recheck this pairing — name
        // length x 26 vs the value's left edge — whenever a row is added.
        Typer::draw(-360, cy, r.name, 13);               // name, left-aligned
        Typer::draw_centered(320, cy, lbl[cur_idx], 18); // value
        // Which WAY the env forces it, after the stored value: "OFF ENV ON"
        // reads as "you set OFF, the environment is forcing ON". Smaller
        // than the value so it clears the name column — at value size it
        // would run into "RECORD REPLAYS".
        if (rec_override >= 0)
          Typer::draw(320 + (int)strlen(lbl[cur_idx]) * 18 + 14, cy,
                      rec_override == 1 ? "ENV ON" : "ENV OFF", 11);
        continue;
      }

      int y = opt_row_center(row, n, desk_opt_top(), desk_opt_bottom());
      MenuSelect::draw_row_cursor(active_row_ == row, CURSOR_L, CURSOR_R, y,
                                12);
      Typer::draw(NAME_X, y, r.name, 12);                // name, left
      if (opener) {
        Typer::draw(VALUE_X, y, "...", 12);              // opens a sub-screen
        continue;
      }
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
    // Desktop: also the selectable row after the last option (index ==
    // the row count); confirm on it closes, confirm on an option row
    // cycles that value (matching the touch tap). On the AUDIO/CAMERA
    // sub-screens the band backs out ONE level, to options, and says so.
    menu_exit_band().draw(
        (audio_mode_ || camera_mode_)
            ? Typer::cursored("BACK TO OPTIONS", touch || active_row_ == n)
                  .c_str()
            : touch ? Typer::cursored("EXIT TO MENU", true).c_str()
                    : Typer::cursored("BACK TO MENU",
                                      active_row_ == opt_row_count())
                          .c_str(),
        currentTime);
  } else if (stats_mode_) {
    bool touch = is_touch_mode();
    Typer::draw_centered(0, menu_screen_heading_y(), "STATS", touch ? 30 : 26);
    // Read-only rows over the same band geometry the options list uses
    // (opt_row_center — one layout rule for every sub-screen). Label left,
    // value right. SPECIAL TYPES is the count over stats.dat's kill mask
    // (reflective and invincible are deliberately absent from it — they
    // die only to god mode, bonus kills rather than requirements); the
    // per-type checklist was tried and CUT — nineteen rows crammed the
    // band (field, 2026-08-19), and the count carries the progress.
    uint32_t mask = Stats::special_kill_mask();
    int found = 0;
    for (int i = 0; i < Stats::SPECIAL_COUNT; i++)
      if (mask & (1u << i)) found++;
    struct StatRow { std::string label, value; };
    std::vector<StatRow> rows;
    rows.push_back({"HIGH SCORE", std::to_string(high_score)});
    uint32_t best_lvl = Stats::highest_level();
    rows.push_back({"HIGHEST LEVEL",
                    best_lvl == 0 ? std::string("-")
                                  : std::to_string(best_lvl)});
    rows.push_back({"GAMES PLAYED",
                    std::to_string(Stats::games_played())});
    // H/M, no seconds: a lifetime total moves too slowly for seconds to
    // say anything, and the shorter string keeps the value column tidy.
    uint32_t secs = Stats::play_seconds();
    rows.push_back({"TIME PLAYED",
                    secs >= 3600
                        ? std::to_string(secs / 3600) + "H " +
                              std::to_string((secs % 3600) / 60) + "M"
                        : std::to_string(secs / 60) + "M"});
    rows.push_back({"DEATHS", std::to_string(Stats::deaths())});
    rows.push_back({"ASTEROIDS DESTROYED",
                    std::to_string(Stats::lifetime_kills())});
    rows.push_back({"SHIPS DESTROYED",
                    std::to_string(Stats::ship_kills())});
    // Shots are primary discharges only (scatter = 1, each burst emission
    // = 1, no turret/secondaries — stats.h), which is what makes the
    // accuracy line mean "the pilot's own trigger". It can honestly
    // exceed 100%: one lance pulse or scatter discharge kills many.
    uint32_t shots = Stats::shots_fired();
    rows.push_back({"SHOTS FIRED", std::to_string(shots)});
    rows.push_back({"ACCURACY",
                    shots == 0 ? std::string("-")
                               : std::to_string((uint32_t)(
                                     (uint64_t)Stats::lifetime_kills() * 100 /
                                     shots)) + "%"});
    rows.push_back({"SECONDARIES USED",
                    std::to_string(Stats::secondaries_used())});
    rows.push_back({"NOVAS DETONATED",
                    std::to_string(Stats::novas_detonated())});
    // 13 glyphs: the label column ends at -360 + 2*sz*len, and the longest
    // label must clear the value column at 160 ("ASTEROIDS DESTROYED", 19
    // glyphs, ends at 96 desktop; "SPECIAL TYPES DESTROYED" at 23 glyphs
    // ran to 192 and collided — the same count-the-glyphs rule as the
    // options name column).
    rows.push_back({"SPECIAL TYPES",
                    std::to_string(found) + " OF " +
                        std::to_string((int)Stats::SPECIAL_COUNT)});
    int n = (int)rows.size();
    float top    = touch ? touch_opt_top()    : desk_opt_top();
    float bottom = touch ? touch_opt_bottom() : desk_opt_bottom();
    const int sz = touch ? 13 : 12;
    for (int i = 0; i < n; i++) {
      int y = opt_row_center(i, n, top, bottom);
      // Values share one right column ("ASTEROIDS DESTROYED" at 19 glyphs
      // ends at 2*sz*19 - 360 = 96 desktop, clear of the column at 160).
      Typer::draw(-360, y, rows[i].label.c_str(), sz);
      Typer::draw(160, y, rows[i].value.c_str(), sz);
    }
    // The band is this screen's only interactive element, so it always
    // carries the cursor marks.
    menu_exit_band().draw(
        Typer::cursored(touch ? "EXIT TO MENU" : "BACK TO MENU", true)
            .c_str(),
        currentTime);
  } else {
    Typer::draw_centered(0, menu_title_y(), "Newtonia", 80);
    if (high_score > 0) {
      // Below the row band, above the copyright (portrait-aware anchors —
      // touch sits a little lower, its row band being taller).
      Typer::draw_centered(0, menu_high_score_y(), "HIGH SCORE", 14);
      Typer::draw_centered(0, menu_high_score_num_y(), high_score, 18);
    }
  }

  if (!options_mode_ && !replays_mode_ && !board_mode_ && !stats_mode_) {
    if (attract_mode_) {
      if (!((currentTime / 1400) % 2)) {
        // Centered between the title bottom (size 80 descends 160) and the
        // high-score block, both portrait-aware.
        const int sz = 18, h = 2 * sz;
        int title_bot = menu_title_y() - 160;
        int scores_top = menu_high_score_y() - (is_touch_mode() ? 0 : 20);
        int gap = (title_bot - scores_top - h) / 2;
        if (is_touch_mode()) {
          Typer::draw_centered(0, title_bot - gap, "tap to start", sz);
        } else {
          bool has_ctrl = pad_count() > 0;
          // Pad prompt in the plugged-in pad's vocabulary (pad.h): START
          // on Xbox, OPTIONS on PlayStation — and under Steam Input,
          // whatever the layout binds the Menu set's start action to.
          char pad_prompt[48];
          // Start dismisses it, and so does Confirm — name whichever the
          // layout binds (pad_action_or).
          snprintf(pad_prompt, sizeof(pad_prompt), "press %s",
                   pad_action_label_any(pad_action_or(PAD_NONE, PAD_ACT_START, PAD_ACT_CONFIRM)));
          Typer::draw_centered(0, title_bot - gap, has_ctrl ? pad_prompt : "press enter", sz);
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
      if (show_stats_row()) rows.push_back("STATS");
      draw_menu_rows(rows);
    }
  }
  if (!options_mode_ && !replays_mode_ && !board_mode_ && !stats_mode_)
    Typer::draw_centered(0, menu_copyright_y(high_score > 0),
                         "© 2008-2026 METONYMOUS", 13, currentTime);
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

#ifdef __EMSCRIPTEN__
  // A leaderboard WATCH deep link (/play/?replay=<season>/<run_id>):
  // main.ts downloaded the blob into download.nrp and web_main.cpp staged
  // it (web_watch_replay). Hand off to the same downloaded-replay playback
  // path the in-game leaderboard uses. The stage step already
  // header-checked the file, so a NULL here (e.g. no leading keyframe) is
  // rare — log and stay in the menu, like the board's FetchDone does.
  if (web_take_replay_watch()) {
    if (GLGame *g = GLGame::start_replay_playback(Replay::download_path())) {
      request_state_change(g);
      return;
    }
    // The header passed its pre-flight but the body didn't parse. Say so —
    // the staging call already answered "accepted", so main.ts has shown
    // nothing and the player would otherwise just sit on the menu
    // wondering what their click did.
    SDL_Log("replay: ?replay= download would not play");
    remove(Replay::download_path().c_str());
    EM_ASM({
      alert("That replay could not be played by this version of the game.");
    });
  }
#endif

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
  // (A Steam Input pad reads 0 here — its trigger arrives as a
  // synthesized confirm edge, so the poll has nothing to add.)
  int n = pad_count();
  if (n > 0) {
    int rt_max = 0;
    PadId rt_id = PAD_NONE;
    for(int i = 0; i < n; i++) {
      PadId id = pad_id_at(i);
      int v = pad_axis(id, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
      if(v > rt_max) {
        rt_max = v;
        rt_id = id;
      }
    }
    SDL_Event e;
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.which = rt_id;
    e.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    e.caxis.value = (Sint16)rt_max;
    unsigned char k = nav_key_from_controller(e);
    if(k) nav_input(k, rt_id);
  }
}

void Menu::controller(SDL_Event event) {
  // Dpad / left stick / A / B / Start / Back / right-trigger all act
  // through the same ladder the keyboard uses — one decision path per
  // screen, so pad directionals and back work wherever keys do. The pad
  // that confirmed rides along so confirm_selection can bind it to P1.
  PadId src = PAD_NONE;
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
  nav_input(key, PAD_NONE);
#endif
}

// The single menu decision ladder (see menu.h). Everything that navigates —
// keyboard, controller buttons/stick, the tick() trigger poll — lands here,
// so each screen's rules exist exactly once.
void Menu::nav_input(unsigned char key, PadId src) {
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
    // The AUDIO/CAMERA sub-screens share this ladder over their own row
    // counts; the only structural difference is where back leads (one
    // level up, to options) — adjust_active_row already reads the right
    // table.
    int rows = audio_mode_ ? audio_row_count()
             : camera_mode_ ? camera_row_count() : opt_row_count();
    // One extra index past the rows: the BACK band (see draw).
    if (MenuSelect::move(key, active_row_, rows + 1)) {
      // moved
    } else if (MenuSelect::is_left(key)) {
      if (active_row_ < rows) adjust_active_row(-1);
    } else if (MenuSelect::is_right(key)) {
      if (active_row_ < rows) adjust_active_row(1);
    } else if (MenuSelect::is_back(key)) {
      if (audio_mode_) close_audio();
      else if (camera_mode_) close_camera();
      else close_options();
    } else if (confirm) {
      // Confirm on the band exits; on an option row it cycles the value
      // (the touch tap's behaviour) — Enter used to close from anywhere,
      // which made the rows the only list in the game confirm did nothing
      // useful on.
      if (active_row_ >= rows) {
        if (audio_mode_) close_audio();
        else if (camera_mode_) close_camera();
        else close_options();
      } else {
        adjust_active_row(1, /*wrap=*/true);
      }
    }
    return;
  }
  if (stats_mode_) {
    // Read-only screen: any exit gesture leaves (the band is the only
    // interactive element, so confirm needs no row test).
    if (MenuSelect::is_back(key) || confirm) stats_mode_ = false;
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
    // One extra index past the rows: the BACK TO MENU band (see draw).
    if (MenuSelect::move(key, replay_sel_, (int)replay_rows_.size() + 1)) {
      // moved
    } else if (MenuSelect::is_back(key) ||
               (confirm && replay_sel_ >= (int)replay_rows_.size())) {
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
      } else if (menu_selection == stats_row_index()) {
        stats_mode_ = true;
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
    if (audio_mode_) close_audio();          // one level: audio -> options
    else if (camera_mode_) close_camera();   // one level: camera -> options
    else close_options();            // persists and returns to the menu
    return true;
  }
  if (replays_mode_) {
    replays_mode_ = false;
    return true;
  }
  if (stats_mode_) {
    stats_mode_ = false;
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
    // end (the AUDIO row opens its sub-screen instead — adjust_active_row
    // owns that fork); the bottom strip exits — one level, so audio backs
    // out to options — on both layouts. The band is drawn on desktop too,
    // where its zone sits well below the deeper desktop rows (rows end
    // ~-285, band reach tops ~-370).
    if (menu_exit_hit().contains(nx, ny)) {
      if (audio_mode_) close_audio();
      else if (camera_mode_) close_camera();
      else close_options();
      return;
    }
    int rows = audio_mode_ ? audio_row_count()
             : camera_mode_ ? camera_row_count() : opt_row_count();
    int row = is_touch_mode()
                  ? touch_opt_row_at(ny, rows)
                  : opt_row_at(ny, rows, desk_opt_top(), desk_opt_bottom());
    if (row >= 0) {
      active_row_ = row;
      adjust_active_row(+1, /*wrap=*/true);
    }
    return;
  }
  if (stats_mode_) {
    if (menu_exit_hit().contains(nx, ny)) stats_mode_ = false;
    return;
  }
  if (board_mode_) {
    if (menu_exit_hit().contains(nx, ny)) { close_board(); return; }
    // Same fixed slots the draw uses (board_entry_y — the TapBand rule).
    float ty = (1.0f - 2.0f * ny) * Typer::scaled_window_height;
    // The range line doubles as the touch pager: left half pages up,
    // right half pages down (drag scrolling is not a thing this menu
    // stack has; the indicator is the one drawn, tappable affordance).
    if ((int)board_rows_.size() > board_visible_rows() &&
        ty <= board_y_range() + board_row_pitch() / 2 &&
        ty >= board_y_range() - board_row_pitch() / 2) {
      int max_scroll = (int)board_rows_.size() - board_visible_rows();
      board_scroll_ += (nx < 0.5f) ? -board_visible_rows() : board_visible_rows();
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
    if (menu_exit_hit().contains(nx, ny)) { replays_mode_ = false; return; }
    int row = is_touch_mode()
                  ? touch_opt_row_at(ny, (int)replay_rows_.size())
                  : opt_row_at(ny, (int)replay_rows_.size(), desk_opt_top(),
                               desk_opt_bottom());
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
        confirm_selection(PAD_NONE);
      } else {
        new_confirm_ = false;
      }
    } else {
      int pick = desktop_confirm_pick(ny);
      if (pick == 0) confirm_selection(PAD_NONE);
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
  if (row == stats_row_index()) {
    stats_mode_ = true;
    return;
  }
  confirm_selection(PAD_NONE);
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
  if (show_stats_row()) n++;
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

// Equally space n row blocks of height h between menu_rows_top() and
// menu_rows_bottom() (the portrait-aware anchors near the top of this
// file). ONE definition shared by draw_menu_rows and menu_row_at so taps
// always land on what is drawn.
static int menu_row_gap(int n, int h) {
  return (menu_rows_top() - menu_rows_bottom() - n * h) / (n + 1);
}

// Touch draws bigger glyphs and no selection cursor; menu_row_at() mirrors
// this exact geometry so taps land on what is drawn.
void Menu::draw_menu_rows(const std::vector<std::string> &rows) {
  const int sz = menu_row_size(), h = 2 * sz;
  int n = (int)rows.size();
  int gap = menu_row_gap(n, h);
  for (int i = 0; i < n; i++) {
    float y = menu_rows_top() - (i + 1) * gap - i * h;
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
  float t = ((float)menu_rows_top() - y) - gap * 0.5f;
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

void Menu::open_audio() {
  audio_mode_ = true;
  active_row_ = 0;
}

void Menu::close_audio() {
  audio_mode_ = false;
  // Land the cursor back on the AUDIO row that opened the screen, so
  // sub-menu round trips don't teleport the selection to the top.
  active_row_ = 0;
  for (int i = 0; i < opt_row_count(); i++)
    if (opt_row(i).kind == 8) { active_row_ = i; break; }
  // The adjusted values are already live (adjust_active_row applies them);
  // persistence rides close_options with everything else.
}

void Menu::open_camera() {
  camera_mode_ = true;
  active_row_ = 0;
}

void Menu::close_camera() {
  camera_mode_ = false;
  // Same round-trip rule as close_audio: land on the CAMERA row.
  active_row_ = 0;
  for (int i = 0; i < opt_row_count(); i++)
    if (opt_row(i).kind == 9) { active_row_ = i; break; }
  // Persistence rides close_options; the zoom prefs are read by pointer
  // (GLShip::set_zoom_prefs), so a live game follows close_options' write.
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
  struct { const char *label; std::string path; } sources[5] = {
      {"CURRENT RUN", Replay::current_path()},
      {"LAST RUN", Replay::recent_path()},
      {"BEST RUN", Replay::best_path_for(1)},
      {"BEST CO-OP", Replay::best_path_for(2)},
      {"ONLINE RUN", Replay::online_path()},
  };
  for (int i = 0; i < 5; i++) {
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

// Does this best slot hold a run the live season's UPLOAD row could offer
// (clean, non-cheated, scored, and stamped with the build's own season)?
// Used only to pick which board the screen OPENS on; the row's real gating
// reads the loaded slot (board_upload_row_shown).
static bool slot_is_upload_candidate(const std::string &path,
                                     const std::string &build_season) {
  Replay::Header h;
  if (!Replay::read_header(path, h)) return false;
  if (!(h.flags & Replay::FLAG_CLEAN) || (h.flags & Replay::FLAG_CHEATED) ||
      h.final_score == 0)
    return false;
  std::string season(h.game_version,
                     strnlen(h.game_version, sizeof(h.game_version)));
  return season == build_season;
}

int Menu::board_row_index() const {
  if (!show_board_row()) return -1;
  int i = base_menu_rows();
  if (show_online_row()) i++;
  if (show_options_row()) i++;
  if (show_replays_row()) i++;
  return i;
}

int Menu::stats_row_index() const {
  if (!show_stats_row()) return -1;
  int i = base_menu_rows();
  if (show_online_row()) i++;
  if (show_options_row()) i++;
  if (show_replays_row()) i++;
  if (show_board_row()) i++;
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
  // Best is per-board (solo best.nrp / co-op best_coop.nrp): open on the
  // board whose slot holds a live-season upload candidate — SOLO wins a
  // tie, and SOLO is the default when neither slot has one. The browsed
  // board's slot itself loads in board_load_best (from board_request).
  board_players_ =
      !slot_is_upload_candidate(Replay::best_path_for(1), board_build_season_) &&
      slot_is_upload_candidate(Replay::best_path_for(2), board_build_season_)
          ? 2 : 1;
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
  board_load_best();  // the browsed board's own best slot (solo/co-op)
  board_loading_ = true;
  board_error_ = false;
  board_rows_.clear();
  board_your_rank_ = 0;
  if (board_sel_ >= board_entry_count()) board_sel_ = 0;
  board_scroll_ = 0;
  board_net_->top(board_season_, board_players_, 100);  // full board; the
                                                        // table windows it
  // The rank-of footer: only meaningful when the slot's best belongs to
  // the browsed season (the slot already matches the player count).
  if (board_best_score_ > 0 && board_best_season_ == board_season_)
    board_net_->rank_of(board_season_, board_players_, board_best_score_);
}

// Load the best slot that belongs to the browsed board (best is per-board:
// solo best.nrp / co-op best_coop.nrp — LEADERBOARD.md). Refreshed on
// every board_request so a SOLO/CO-OP flip swaps the upload candidate, the
// " - YOU" row tag and the rank-of footer along with the rows.
void Menu::board_load_best() {
  board_best_run_id_.clear();
  board_best_season_.clear();
  board_best_score_ = 0;
  board_best_clean_ = false;
  Replay::Header h;
  if (!Replay::read_header(Replay::best_path_for((uint8_t)board_players_), h))
    return;
  char rid[24];
  snprintf(rid, sizeof(rid), "%llu", (unsigned long long)h.run_id);
  board_best_run_id_ = rid;
  board_best_season_.assign(h.game_version,
                            strnlen(h.game_version, sizeof(h.game_version)));
  board_best_score_ = h.final_score;
  board_best_clean_ = (h.flags & Replay::FLAG_CLEAN) &&
                      !(h.flags & Replay::FLAG_CHEATED) && h.final_score > 0;
  // Keep this slot's season reachable in the browser: the UPLOAD row lives
  // on ITS season's screen, and the worker only lists seasons that already
  // have rows — an old-season best would otherwise be unreachable (nothing
  // to cycle to) until someone else charted there.
  if (!board_best_season_.empty()) {
    for (const NetBoard::Season &s : board_seasons_)
      if (s.season == board_best_season_) return;
    NetBoard::Season bs;
    bs.season = board_best_season_;
    board_seasons_.push_back(bs);
  }
}

// board_up_phase_, seen from the browsed board: an upload's transfer/status
// belongs to the board it was started for (board_up_players_) — the other
// board's UPLOAD row must read idle, not wear a foreign UPLOADED #N label.
int Menu::board_up_phase_shown() const {
  return board_up_players_ == board_players_ ? board_up_phase_ : 0;
}

int Menu::board_entry_y(int e) const {
  if (e == 0) return board_y_toggle();
  if (e == 1) return board_y_season();
  int ri = e - 2;
  if (ri < (int)board_rows_.size()) {
    if (ri < board_scroll_ || ri >= board_scroll_ + board_visible_rows())
      return BOARD_Y_OFFSCREEN;  // scrolled out of the window
    return board_y_row0() - (ri - board_scroll_) * board_row_pitch();
  }
  if (board_upload_row_shown() && ri == (int)board_rows_.size())
    return board_y_upload();  // the UPLOAD BEST RUN action
  // The trailing BACK TO MENU entry: the band draws its own label and
  // catches its own taps (TapBand::contains runs before board_entry_at),
  // so the row machinery must never draw or hit-test a slot for it.
  return BOARD_Y_OFFSCREEN;
}

// Slide the window so the selected entry is visible (rows only — the
// fixed slots always are). Called after every selection move.
void Menu::board_ensure_visible() {
  int ri = board_sel_ - 2;
  if (ri < 0 || ri >= (int)board_rows_.size()) return;
  if (ri < board_scroll_) board_scroll_ = ri;
  if (ri >= board_scroll_ + board_visible_rows())
    board_scroll_ = ri - board_visible_rows() + 1;
}

int Menu::board_entry_at(float y) const {
  int n = board_entry_count();
  for (int e = 0; e < n; e++) {
    // The single-line control rows (BOARD toggle, SEASON) carry their own
    // zone size: on touch a generous ±50 that their 104 separation keeps
    // overlap-free; the score rows use the row pitch. Desktop keeps the
    // uniform classic half-pitch (24) everywhere.
    int half = (e <= 1 && is_touch_mode()) ? 50 : board_row_pitch() / 2;
    if (y <= board_entry_y(e) + half && y >= board_entry_y(e) - half)
      return e;
  }
  return -1;
}

int Menu::board_entry_count() const {
  // [0] SOLO/CO-OP toggle, [1] SEASON browser, [2..] score rows, then the
  // UPLOAD BEST RUN action when it applies to the browsed season, and
  // always last: the BACK TO MENU band as a selectable entry (it draws and
  // hit-tests itself — board_entry_y hides it from the row machinery).
  return 2 + (int)board_rows_.size() + (board_upload_row_shown() ? 1 : 0) + 1;
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
  if (!(board_best_clean_ && net_board_can_submit() &&
        board_best_season_ == board_season_ &&
        (board_net_ != nullptr || board_up_phase_shown() >= 2)))
    return false;
  // Nothing to offer when the best is ALREADY on the board: a fetched row
  // carrying this exact run at (or above) the local score means an upload
  // could only be refused already-submitted. Same-run-but-higher-local
  // (a clean-abandoned upload later resumed and improved) keeps the row —
  // that upload upserts the better score. Only the IDLE row hides: once
  // an upload ran this session the row is also its status line
  // (UPLOADED #N / failed), which must not vanish on the post-placed
  // refresh.
  if (board_up_phase_shown() == 0) {
    // ...and that check can only answer once the top rows have ARRIVED:
    // while the fetch is in flight the row stays hidden rather than
    // flashing an offer that the loaded rows then retract (field report
    // 2026-08-03). The rows land together with board_loading_ clearing,
    // so this gate and the loop below always agree.
    if (board_loading_) return false;
    if (!board_best_run_id_.empty()) {
      for (const NetBoard::Row &r : board_rows_)
        if (r.run_id == board_best_run_id_ && r.score >= board_best_score_)
          return false;
    }
  }
  return true;
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
  board_up_players_ = board_players_;  // the board this upload belongs to
  const NetIdentity &me = net_local_identity();
  std::string cred = net_board_verify_credential();
  board_up_sent_cred_ = cred;  // for the retry's freshness compare
  board_net_->submit(Replay::best_path_for((uint8_t)board_up_players_),
                     me.platform, me.name, cred);
  board_up_phase_ = 1;
  SDL_Log("board: uploading %s (menu)",
          board_up_players_ == 2 ? "best_coop.nrp" : "best.nrp");
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
  if (board_sel_ == board_entry_count() - 1) {  // the BACK TO MENU band
    close_board();
    return;
  }
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
      (board_up_phase_shown() == 0 || board_up_phase_shown() == 3))
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
      board_net_->submit(Replay::best_path_for((uint8_t)board_up_players_),
                         me.platform, me.name, fresh);
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
        // Drop an answer for a board/season we have since flipped away
        // from (echoed by the worker; 0/"" = an old worker that echoes
        // nothing). Handlers answer out of order under load, so rapid
        // SOLO/CO-OP flips could land the OTHER board's answer last —
        // the CO-OP screen sat on SOLO's empty rows (field, 2026-08-03).
        if (ev.players != 0 && ev.players != board_players_) break;
        if (!ev.season.empty() &&
            net_board_sanitize(ev.season, 22) != board_season_)
          break;
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
  const OptRow &r = audio_mode_ ? audio_row(active_row_)
                  : camera_mode_ ? camera_row(active_row_)
                                 : opt_row(active_row_);
  int *idx, num;
  switch (r.kind) {
    case 0: idx = &sensitivity_index_[r.player]; num = NUM_SENSITIVITY;  break;
    case 1: idx = &smoothing_index_[r.player];   num = NUM_SMOOTHING;    break;
    case 2: idx = &camera_index_[r.player];      num = NUM_CAMERA;       break;
    case 3: idx = &star_density_index_;          num = NUM_STAR_DENSITY; break;
    case 5: idx = &leaderboard_index_;           num = NUM_LEADERBOARD;  break;
    case 6: idx = &master_volume_index_;         num = NUM_VOLUME;       break;
    case 7: idx = &music_volume_index_;          num = NUM_VOLUME;       break;
    case 8:
      // The opener rows have no value to cycle — every adjust gesture
      // (left/right, confirm, tap) opens the sub-screen instead.
      open_audio();
      return;
    case 9:
      open_camera();
      return;
    case 10: idx = &zoom_index_[r.player];       num = NUM_ZOOM;         break;
    case 11: idx = &speed_zoom_index_[r.player]; num = NUM_SPEED_ZOOM;   break;
    case 12: idx = &input_index_;                num = NUM_INPUT;        break;
    case 13: idx = &handedness_index_;           num = NUM_HANDEDNESS;   break;
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
  if (r.kind == 6 || r.kind == 7) {
    // Volumes apply LIVE, not on close: the menu music is playing right
    // now, and hearing the step land is the whole feedback loop (the
    // stored prefs are written with the rest by close_options).
    g_prefs.master_volume = VOLUME_VALUES[master_volume_index_];
    g_prefs.music_volume  = VOLUME_VALUES[music_volume_index_];
    AudioVolume::apply();
  }
}

void Menu::close_options() {
  audio_mode_ = false;   // safety: closing options closes its sub-screens too
  camera_mode_ = false;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    g_prefs.player_keys[i].keyboard_sensitivity = SENSITIVITY_VALUES[sensitivity_index_[i]];
    g_prefs.player_keys[i].camera_smoothing     = SMOOTHING_VALUES[smoothing_index_[i]];
    g_prefs.player_keys[i].rotate_view          = (camera_index_[i] == 1);
    g_prefs.player_keys[i].camera_zoom          = ZOOM_VALUES[zoom_index_[i]];
    g_prefs.player_keys[i].speed_zoom           = SPEED_ZOOM_VALUES[speed_zoom_index_[i]];
  }
  g_prefs.star_density                 = STAR_DENSITY_MULTIPLIERS[star_density_index_];
  g_prefs.auto_record_replays          = (auto_record_index_ == 1);
  g_prefs.leaderboard_prompts          = (leaderboard_index_ == 1);
  g_prefs.master_volume                = VOLUME_VALUES[master_volume_index_];
  g_prefs.music_volume                 = VOLUME_VALUES[music_volume_index_];
  g_prefs.touch_one_hand               = (input_index_ == 1);
  g_prefs.touch_handedness             = handedness_index_;
  save_preferences();
  // The joystick hint/radius live in the touch layout, so re-run it in
  // place — the next game must open in the picked input method without
  // waiting for a window resize.
  touch_controls_relayout();
#ifdef __EMSCRIPTEN__
  // The web build's OSD is HTML (web/main.ts): hand it the mode and the
  // handedness side (-1/0/+1) so it can rebuild its own layout, over the
  // same bridge setMenuMode rides.
  EM_ASM({ if (window.setOneHandMode) window.setOneHandMode($0, $1); },
         g_prefs.touch_one_hand ? 1 : 0, g_prefs.touch_handedness - 1);
#endif
  delete starfield;
  starfield = new GLStarfield(Point(default_world_width, default_world_height),
                              STAR_DENSITY_MULTIPLIERS[star_density_index_],
                              0.0f /* menu camera at the origin */);
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

void Menu::confirm_selection(PadId pad) {
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
      // A resumed run has REACHED its level (the rollover-only note left
      // "-" for anyone who never cleared one). Here at the menu, not in
      // the save ctor: the replay-playback ctor delegates through it, and
      // watching must bank nothing.
      if (!s.cheated) Stats::note_level_reached((uint32_t)s.generation + 1);
      request_state_change(new GLGame(s, code, token, pad));
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
      // Same reached-level note as the RESUME HOSTING path above.
      if (!s.cheated) Stats::note_level_reached((uint32_t)s.generation + 1);
      request_state_change(new GLGame(s, pad));
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
  request_state_change(new GLGame(pad));
}
