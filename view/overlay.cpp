#include "overlay.h"

#include "../menu_select.h"
#include "tap_band.h"
#include "../net_board.h"
#include "../net_identity.h"
#include "../net_session.h"
#include "../net_transport.h"
#include "../glship.h"
#include "../glgame.h"
#include "../typer.h"
#include "../ship.h"
#include "../touch_controls.h"
#include "../preferences.h"
#include "../replay.h"
#include <cctype>

#include "../gl_compat.h"
#include "../mat4.h"
#include "../mesh.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

const float Overlay::CORNER_INSET = 55.0f;

#ifdef _GAMING_XBOX
const float Overlay::SAFE_AREA_SCALE = 0.9f;
#else
const float Overlay::SAFE_AREA_SCALE = 1.0f;
#endif

// Display-cutout insets in physical pixels (top, bottom, left, right); see
// overlay.h. NEWTONIA_SAFE_INSET_TOP=N forces the top inset on any platform
// so the notch layout is testable without cutout hardware.
static float s_safe_inset[4] = {0.0f, 0.0f, 0.0f, 0.0f};

// Pause-menu row geometry (Overlay::paused). Sized to sit under "Paused"
// (anchor 30, size 25, glyphs to -20) and still finish above the
// disconnect overlay's "PRESS FIRE FOR MENU" at y=-130.
static const int PAUSE_ROW_SZ = 13, PAUSE_ROW_Y0 = -42, PAUSE_ROW_GAP = 38;

// The always-on "ROOM <code>" line: its size and its y anchor as a fraction
// of the FULL-window half-height. One definition, because the banner is not
// the only thing that needs it — the centre-column countdowns underneath
// have to know where it ends (room_line_drop).
static const float ROOM_LINE_SZ = 18.0f;
static const float ROOM_LINE_Y = 0.67f;

void Overlay::set_safe_insets(float top, float bottom, float left, float right) {
  s_safe_inset[0] = top;
  s_safe_inset[1] = bottom;
  s_safe_inset[2] = left;
  s_safe_inset[3] = right;
}

float Overlay::safe_inset_top() {
  static float forced = -1.0f;
  if (forced < 0.0f) {
    const char *env = SDL_getenv("NEWTONIA_SAFE_INSET_TOP");
    forced = (env != NULL) ? (float)atof(env) : 0.0f;
  }
  return (forced > 0.0f) ? forced : s_safe_inset[0];
}

// The cutout's top inset in Typer virtual units: the HUD ortho spans
// ±viewport pixels (so one physical pixel = two ortho units) and Typer
// coords are multiplied by Typer::scale — hence px * 2 / scale.
static float safe_inset_top_v() {
  return Overlay::safe_inset_top() * 2.0f / Typer::scale;
}

// The top-anchored HUD row's y anchor in Typer virtual units: the title-safe
// top, pulled down by the cutout inset.
static float top_hud_y(const GLGame *glgame) {
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  return vh - 20 - Overlay::CORNER_INSET - safe_inset_top_v();
}

// True in the 3-4 player 2x2 grid, where a viewport is a quarter of the
// window: both viewport counts are >= 2 only there (1P is 1x1, and either
// 2P split keeps one axis at 1).
static bool viewport_is_grid_cell(const GLGame *glgame) {
  return glgame->num_x_viewports() >= 2 && glgame->num_y_viewports() >= 2;
}

// A grid cell carries the same HUD as a full window in a quarter of the
// room, so the full-size rows and centre banners crowd each other (field:
// CLEARED overprinted the weapons list). Shrink the HUD text there; 1P and
// both 2P splits are untouched.
static float hud_fit(const GLGame *glgame) {
  return viewport_is_grid_cell(glgame) ? 0.75f : 1.0f;
}


// Full-screen text layered over the online game view (one full-screen
// pass, not per-viewport): the 2 s generation banner (replaces the offline
// Intro state) and the CONNECTION LOST / rejoin card. Overlay is a friend
// of GLGame, so it reads the net_* state directly.
// Replay playback chrome (REPLAY.md R2): one full-screen pass drawn after
// net_overlays. Watermark + speed top-centre, elapsed/total bottom-centre,
// and the flashing exit hint when the recording ends without a game over
// (the shared GAME OVER card handles the other ending).
void Overlay::replay_hud(const GLGame *glgame) {
  glViewport(0, 0, glgame->window.x(), glgame->window.y());
  float hw = glgame->window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = glgame->window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  float vh = Typer::scaled_window_height;
  int now = glgame->current_time;
  bool touch = is_touch_mode();

  // The replay HUD lives at the BOTTOM (at the top the REPLAY watermark
  // collided with the per-viewport LEVEL text). Stack, top to bottom:
  // watermark, timeline, then the controls hint — lifted well clear of the
  // screen edge. Typer draws each glyph offset DOWN by 2*size (see
  // pre_draw), so a line's real footprint is ~[y-2*size, y] and a bigger
  // font drops further: the gaps below clear 2*size + margin per line, or
  // the watermark overlaps the line under it. On touch the controls are tap
  // bands lower down, so only the watermark + timeline sit here, above them.
  //
  // The desktop anchors are written against the bottom edge (-vh + K)
  // already; the touch ones are the landscape constants they were tuned as,
  // re-anchored by the same TapBand::bottom_lift() their tap bands use, so
  // the whole touch stack stays one unit. Without it portrait strands this
  // chrome mid-screen, right on top of the action (field: Moto E14,
  // 2026-07-27) — the exact bug GLGame::exit_band() was re-anchored for.
  float lift = TapBand::bottom_lift();
  char text[48];
  if (glgame->replay_speed_ != 1.0f) {
    snprintf(text, sizeof(text), "REPLAY x%g", (double)glgame->replay_speed_);
  } else {
    snprintf(text, sizeof(text), "REPLAY");
  }
  Typer::draw_centered(0, touch ? -155.0f + lift : -vh + 295, text, 18);

  // last_slot() is bounded by the reader's slot sanity cap so this multiply
  // stays inside an int (Replay::MAX_RECORD_SLOT — the file's slot indices
  // are not otherwise this build's to trust).
  int total_ms = glgame->replay_reader_
                     ? (glgame->replay_reader_->last_slot() + 1) * 100
                     : 0;
  int elapsed_ms = glgame->replay_clock_ms_;
  if (elapsed_ms > total_ms) elapsed_ms = total_ms;
  if (elapsed_ms < 0) elapsed_ms = 0;
  snprintf(text, sizeof(text), "%d:%02d / %d:%02d", elapsed_ms / 60000,
           (elapsed_ms / 1000) % 60, total_ms / 60000,
           (total_ms / 1000) % 60);
  Typer::draw_centered(0, touch ? -235.0f + lift : -vh + 220, text, 14);

  // On-screen controls. Touch: real tap targets (one definition with the
  // hit-tests in GLGame::touch_tap — the TapBand rule). Desktop/controller:
  // a dim hint line under the timeline, from live bindings.
  if (touch) {
    // The three-way transport strip stays unmarked: the cursor says "this
    // one is picked", and all three of these are live at once.
    TapBand::replay_slower.lifted(lift).draw("SLOWER");
    TapBand::replay_pause.lifted(lift).draw(glgame->running ? "PAUSE"
                                                            : "RESUME");
    TapBand::replay_faster.lifted(lift).draw("FASTER");
    // Lifted with the rest: leaving the exit band on its fixed anchor
    // would put it ABOVE the transport strip in portrait, inverting the
    // stack. to_bottom still carries its tap region to the screen edge.
    TapBand::return_to_menu.lifted(lift).draw(
        Typer::cursored("EXIT TO MENU", true).c_str(), now);
  } else {
    bool has_ctrl = false;
    int nc = SDL_NumJoysticks();
    for (int i = 0; i < nc; i++)
      if (SDL_IsGameController(i)) { has_ctrl = true; break; }
    if (has_ctrl) {
      snprintf(text, sizeof(text), "START PAUSE   B MENU");
    } else {
      const GeneralKeys &gk = g_prefs.general_keys;
      snprintf(text, sizeof(text), "%c PAUSE   %c/%c SPEED   ESC MENU",
               toupper(gk.pause), (char)gk.time_speed_up,
               (char)gk.time_slow_down);
    }
    Typer::draw_centered(0, -vh + 155, text, 9);
  }

  if (glgame->replay_finished_ && !glgame->game_over) {
    Typer::draw_centered(0, -40, "REPLAY ENDED", 16);
    // The exit affordance every other end-state uses; touch has the band
    // above (replay_hud draws it unconditionally).
    if (!is_touch_mode())
      MenuSelect::draw_row(-100, "EXIT TO MENU", 16, true);
  }
}

void Overlay::net_overlays(const GLGame *glgame) {
  // All players out = the game ended. Lives replicate, so both roles see
  // this at the same moment — and it outranks every connection card: the
  // host leaving right after game over used to greet the client with
  // "THE HOST LEFT THE GAME", so the game never LOOKED finished there
  // (Glenn: "no gameover state for the client, it just disconnects").
  // all_players_out() also folds in the game_over latch: it fires with
  // all-out, and also when a spectating player loses the peer (terminal —
  // nothing left to rejoin for). Treat that as game over so the card, not
  // the reconnect notice, is the ending in that case.
  bool all_game_over = glgame->all_players_out();

  // The room code stays up for the WHOLE session, not just after a drop
  // (field request): the host reads it out to whoever is joining, a seat
  // can open at any moment, and hunting for it meant making someone
  // disconnect first. Drawn at the same spot the loss notice used to put
  // it, so nothing moves when a player actually does leave — that branch
  // no longer draws its own copy.
  // room_line_shown() also answers this for the countdowns below it, which
  // step down out of its way (room_line_drop) — one condition, so the space
  // can't be reserved by one and used by the other.
  bool show_room = room_line_shown(glgame);

  if (!all_game_over && glgame->net_banner_ms_ <= 0 &&
      !glgame->net_any_peer_lost() && !show_room)
    return;

  glViewport(0, 0, glgame->window.x(), glgame->window.y());
  float hw = glgame->window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = glgame->window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  // Typer scales all coordinates by Typer::scale (the 800x600-based virtual
  // HUD space every Overlay position lives in) — pixel-derived positions
  // like hh*0.72 only line up when the window happens to be near 800x600
  // and drift offscreen in fullscreen. Position in virtual units instead;
  // vh is the virtual half-height (the top of the title-safe area).
  float vh = Typer::scaled_window_height;
  int now = glgame->current_time;

  // Not under the touch roster: its PLAYERS heading sits on this line's
  // spot, and the code re-appears the moment the roster closes.
  if (show_room && !(is_touch_mode() && glgame->roster_open())) {
    std::string room = "ROOM " + glgame->net_room_code_;
    Typer::draw_centered(0, vh * ROOM_LINE_Y, room.c_str(), ROOM_LINE_SZ);
  }

  if (all_game_over) {
    // The offline hints live in the single-player title_text branch, so
    // the online game (always 2 players) had NO game-over text on either
    // screen. One shared card for both roles; the 3 s guard in the input
    // handlers still stops a mid-fight trigger from skipping it.
    // When a leaderboard flow is live, board_prompt() owns the ENTIRE
    // game-over card (heading + prompt/result + its own EXIT TO MENU
    // row), in every mode — so this online card cedes it completely to
    // avoid drawing GAME OVER (and the row) twice.
    if (glgame->board_phase_ != GLGame::BoardOff) return;
    Typer::draw_centered(0, 60, "GAME OVER", 34);
    // Every screen whose only move is "leave" says so the same way: the
    // shared EXIT TO MENU row, answered by confirm or back. Touch draws
    // no cursor and already has the tap band under this card (title_text
    // shows it whenever the game is over), so the row would just repeat it.
    if (!is_touch_mode())
      MenuSelect::draw_row(-80, "EXIT TO MENU", 16, true);
    // The offline card's recording notice, on the shared card too
    // (LEADERBOARD.md L7 — the field case was exactly this screen: a co-op
    // host with recording off, mystified while the peer's phone charted).
    // Same lowercase hint register — an aside under the card, not a card
    // row. Not during playback — a watched replay's ending is not the
    // moment for options advice. Below the exit row (-80 descends to
    // -112), above the hoisted per-seat score rows (top out at -231).
    if (glgame->net_mode_ != GLGame::NetReplay &&
        !Replay::recording_enabled() && net_board_can_submit())
      Typer::draw_centered(0, -150,
                           "turn on record replays to enter scores",
                           is_touch_mode() ? 10 : 8);
    return;
  }

  // The one lost-link state that is a quiet notice, not a card, is exactly
  // the state net_card_owns_input() excludes — ask the game rather than
  // re-derive the condition here, or the two copies drift and the overlay
  // draws a card whose input the handlers aren't answering (the pause-row
  // lesson, overlay.cpp paused()).
  if (glgame->net_all_peers_lost() && !glgame->net_card_owns_input()) {
    // Rejoinable loss: the game continues — a quiet notice, not a card.
    // A "P2 DISCONNECTED" header over the room code (steady, no blink): the
    // host may be reading the code out to the other player, and it explains
    // why the code is back on screen. Named when the peer's identity is
    // known ("GLENN DISCONNECTED"); a legacy peer keeps the plain text.
    // A LAN-door session has no code — the reopened beacon is the way
    // back, so say that instead.
    // Post-B7 a room can lose more than one seat: count them, name the
    // single-loss case as before (the 2P wording, byte-identical), and
    // say the count when several are out at once.
    int lost = 0;
    for (const GLGame::NetPeer *p : glgame->net_peers_)
      if (p->lost) lost++;
    std::string head;
    if (lost > 1) {
      char buf[40];
      snprintf(buf, sizeof buf, "%d PLAYERS DISCONNECTED", lost);
      head = buf;
    } else {
      head = net_identity_name_or(glgame->net_peer_identity(),
                                  glgame->net_peer_fallback().c_str(),
                                  glgame->net_id_ctx()) +
             " DISCONNECTED";
    }
    Typer::draw_centered(0, vh * 0.80f, head.c_str(), 20);
    // The room line itself is the always-on one above — this branch used to
    // draw its own copy, which is why the code only ever appeared here.
    if (glgame->net_lan_door_open())
      Typer::draw_centered(0, vh * (glgame->net_signal_ ? 0.57f : 0.67f),
                           "VISIBLE ON THIS NETWORK", 14);
  } else if (glgame->net_all_peers_lost() &&
             glgame->net_mode_ == GLGame::NetClient &&
             (!glgame->net_room_code_.empty() ||
              !glgame->net_lan_host_name_.empty())) {
    // y=160, not 60: clear of the pause overlay's "Paused" at y=30 — the
    // terminal branch below moved for this same collision, and this one
    // stacks with "Paused" the same way (host pauses the shared game, then
    // its process dies without BYE while the room code is live).
    Typer::draw_centered(0, 160, "CONNECTION LOST", 34);
    if ((now / 700) % 2 == 0)
      Typer::draw_centered(0, -80, "REJOINING", 16);
    // The exits stay live during a rejoin — confirm/back abandons it for
    // the menu — so the affordance must be drawn (drawn and interactive
    // agree): the terminal card's shared row, same spot. Touch draws no
    // cursor and has the exit band instead.
    if (!is_touch_mode())
      MenuSelect::draw_row(-130, "EXIT TO MENU", 16, true);
  } else if (glgame->net_all_peers_lost()) {
    if (glgame->net_kicked_) {
      // Checked before the BYE wording (a kick sets both): the player was
      // removed, and telling them the host left would send them straight
      // back to the room code wondering why it stopped working.
      Typer::draw_centered(0, vh * 0.55f, "REMOVED FROM THE GAME", 22);
    } else if (glgame->net_peer_bye_) {
      // Named like DISCONNECTED/RECONNECTED but near the middle, at the
      // banner spot ("GLENN LEFT THE GAME"; the host is player 1, so a
      // nameless/legacy host reads "PLAYER 1 LEFT THE GAME"). Clear of the
      // pause overlay's "Paused" at y=30 — both show when the host
      // leaves a paused game.
      std::string who =
          net_identity_name_or(glgame->net_peer_identity(),
                               glgame->net_peer_fallback().c_str(),
                               glgame->net_id_ctx());
      Typer::draw_centered(0, vh * 0.55f, (who + " LEFT THE GAME").c_str(),
                           22);
    } else {
      // y=160, not 60: clear of the pause overlay's "Paused" at y=30.
      Typer::draw_centered(0, 160, "CONNECTION LOST", 34);
    }
    // y=-130: clear of where the pause overlay's menu rows sit (RESUME at
    // -42, EXIT TO MENU at -80, glyphs reaching ~-106). Those rows are
    // never drawn beside this card any more — pause_menu_active() refuses
    // while the card owns input, so a loss landing on a paused game keeps
    // the "Paused" heading but not the menu under it (the host pausing and
    // then leaving used to give the client two dead rows above this live
    // one). The position keeps the historical gap so nothing shifts.
    // A menu row, not an any-key prompt: confirm or back leaves and
    // nothing else does, so a stray keypress can't end the session.
    // Steady, not flashing — a cursor row is a thing you act on, not an
    // alert. Touch draws no cursor and takes the tap band instead, which
    // title_text now shows on a lost link like it does at game over.
    if (!is_touch_mode())
      MenuSelect::draw_row(-130, "EXIT TO MENU", 16, true);
  } else if (glgame->net_banner_ms_ > 0) {
    // A header banner ("<NAME> RECONNECTED") takes the DISCONNECTED
    // header's exact position and size, so the notice swaps in place
    // rather than jumping down the screen when the peer returns.
    if (glgame->net_banner_header_)
      Typer::draw_centered(0, vh * 0.80f, glgame->net_banner_text_.c_str(), 20);
    else
      // banner_spot_y, not a bare vh*0.55: with the countdown stack
      // dropped under the room line, the generation banner overprinted
      // the god-mode seconds at the old fixed spot.
      Typer::draw_centered(0, banner_spot_y(glgame),
                           glgame->net_banner_text_.c_str(), 22);
  } else if (glgame->net_any_peer_lost() &&
             glgame->net_mode_ == GLGame::NetHost) {
    // Play-on partial loss (post-B7, PB-D7): a seat dropped but the game
    // keeps running for the healthy ones — the quiet header treatment,
    // never a card. Name the LOWEST lost seat (the one the rejoin door is
    // serving first) and put the room code back on screen, like the
    // all-lost notice: the host may be reading it out to the friend
    // relaunching. A second simultaneous loss rides the count. NOT at
    // the all-lost spot (vh*0.80): that precedent only ever showed over
    // an effectively-paused game, while this one sits over LIVE play —
    // vh*0.80/0.67 ink lands exactly on the god-mode (y 443..463) and
    // time-slow (y 368..388) indicators, both plausibly running in the
    // "keep playing" state. vh*0.55 is the banner spot, proven for live
    // gameplay text, and the rows below it stay clear of both.
    int lost = 0, low_seat = 0;
    for (const GLGame::NetPeer *p : glgame->net_peers_)
      if (p->lost) {
        lost++;
        if (!low_seat || (int)p->seat < low_seat) low_seat = (int)p->seat;
      }
    std::string head;
    if (lost > 1) {
      char buf[40];
      snprintf(buf, sizeof buf, "%d PLAYERS DISCONNECTED", lost);
      head = buf;
    } else {
      char role[16];
      snprintf(role, sizeof role, "PLAYER %d", low_seat);
      head = net_identity_name_or(glgame->net_identity_for_seat(low_seat),
                                  role, glgame->net_id_ctx_for_seat(low_seat)) +
             " DISCONNECTED";
    }
    // The same banner-spot rule as the generation banner: with the
    // countdown stack dropped this header's old fixed vh*0.55 sat on the
    // god-mode seconds — and this notice is PERSISTENT, not a 2 s flash.
    Typer::draw_centered(0, banner_spot_y(glgame), head.c_str(), 20);
    if (glgame->net_signal_ && !show_room) {
      // Only when the always-on room line is NOT already up (it nearly
      // always is here — a worker session has a code and the game plays
      // on): a second copy of the same line duplicated it, and with the
      // countdowns dropped this spot sits on the time-slow rows.
      std::string room = "ROOM " + glgame->net_room_code_;
      Typer::draw_centered(0, vh * 0.44f, room.c_str(), 18);
    }
    if (glgame->net_lan_door_open())
      Typer::draw_centered(0, vh * (glgame->net_signal_ ? 0.36f : 0.44f),
                           "VISIBLE ON THIS NETWORK", 14);
  }
}

void Overlay::draw(const GLGame *glgame, const GLShip *glship) {
  // Replay playback: keep the run's own HUD (score/lives/weapons/level —
  // part of the story) but drop every overlay that invites input the
  // replay won't take — join/help hints, the keymap card, the touch OSD
  // (replay_hud carries the touch exit hint; R3 owns richer touch UX).
  bool replaying = glgame->net_mode_ == GLGame::NetReplay;
  // The touch roster is a full-screen list: the room line, badge rows and
  // OSD circles under its dim collided with the blocks (the score/level
  // strip stays, matching what the desktop roster leaves visible).
  bool roster_up = is_touch_mode() && glgame->roster_open();
  if (!replaying && !roster_up) title_text(glgame, glship);
  level(glgame, glship);
  god_mode(glgame, glship);
  time_slow(glgame, glship);
  score(glgame, glship);
  level_cleared(glgame, glship);
  lives(glgame, glship);
  weapons(glgame, glship);
  temperature(glgame, glship);
  respawn_timer(glgame, glship);
  spectate(glgame, glship);
  if (!roster_up) net_badges(glgame, glship);
  if (!replaying && !roster_up) touch_controls(glgame, glship);
  edge_indicators(glgame, glship);
  // Last on purpose: keymap dims this whole viewport under the card, so
  // everything above (HUD rows, edge indicators, the world) recedes and
  // the card reads clean — the pause-menu dim's per-viewport twin.
  if (!replaying) keymap(glgame, glship);
  if (glgame->debug_grid) debug_info(glgame, glship);
}

void Overlay::edge_indicators(const GLGame *glgame, const GLShip *glship) {
  if (!glship->ship->is_alive()) return;
  if (glgame->objects->empty()) return;

  int nx = glgame->num_x_viewports();
  int ny = glgame->num_y_viewports();

  // hw_vis: full viewport half-width in ortho coords — used for world→screen scaling
  // and on-screen visibility checks (asteroids visible anywhere on screen are excluded).
  // hw: capped to 16:9 — used only for arrow placement so arrows stay in the OSD zone.
  float hw_vis = (float)Typer::window_width / nx;
  float hw     = Typer::scaled_window_width / nx * Typer::scale;
  float hh = (float)Typer::window_height / ny;

  float fov_deg = (ny == 1) ? glship->view_angle() : glship->view_angle() * 0.75f;
  float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
  float aspect = ((float)glgame->window.x() / nx) / ((float)glgame->window.y() / ny);
  float half_w = half_h * aspect;

  float scale_x = hw_vis / half_w;
  float scale_y = hh / half_h;

  float dir_deg = glship->rotate_view() ? glship->camera_facing() : 0.0f;
  float dir_rad = dir_deg * (float)M_PI / 180.0f;
  float cos_d = cosf(dir_rad);
  float sin_d = sinf(dir_rad);

  Point ship_pos = glship->ship->position;

  const float inset      = 40.0f;
  const float arrow_size = 48.0f;
  float edge_hw = hw - inset;
  float edge_hh = hh - inset;

  // Only show off-screen arrows when no killable asteroids are visible on screen
  for (auto it = glgame->objects->cbegin(); it != glgame->objects->cend(); ++it) {
    const Asteroid *a = *it;
    if (!a->alive || a->invincible) continue;
    Point closest = a->position.closest_to(ship_pos);
    float wdx = closest.x() - ship_pos.x();
    float wdy = closest.y() - ship_pos.y();
    float sx = (wdx * cos_d - wdy * sin_d) * scale_x;
    float sy = (wdx * sin_d + wdy * cos_d) * scale_y;
    float r_sx = a->effective_radius() * scale_x;
    float r_sy = a->effective_radius() * scale_y;
    if (sx - r_sx < hw_vis && sx + r_sx > -hw_vis && sy - r_sy < hh && sy + r_sy > -hh) return;
  }

  static MeshBuilder mb;
  static Mesh mesh;
  mb.clear();
  mb.begin(GL_TRIANGLES);
  mb.color(1.0f, 1.0f, 1.0f, 1.0f);

  for (auto it = glgame->objects->cbegin(); it != glgame->objects->cend(); ++it) {
    const Asteroid *a = *it;
    if (!a->alive || a->invincible) continue;

    Point closest = a->position.closest_to(ship_pos);
    float wdx = closest.x() - ship_pos.x();
    float wdy = closest.y() - ship_pos.y();
    float sx = (wdx * cos_d - wdy * sin_d) * scale_x;
    float sy = (wdx * sin_d + wdy * cos_d) * scale_y;
    float r_sx = a->effective_radius() * scale_x;
    float r_sy = a->effective_radius() * scale_y;
    if (sx - r_sx < hw_vis && sx + r_sx > -hw_vis && sy - r_sy < hh && sy + r_sy > -hh) continue;

    float tx = (fabsf(sx) > 1e-6f) ? edge_hw / fabsf(sx) : 1e9f;
    float ty = (fabsf(sy) > 1e-6f) ? edge_hh / fabsf(sy) : 1e9f;
    float t  = (tx < ty) ? tx : ty;
    float ax = fmaxf(fminf(sx * t, edge_hw), -edge_hw);
    float ay = fmaxf(fminf(sy * t, edge_hh), -edge_hh);

    float angle   = atan2f(sy, sx);
    float cos_a   = cosf(angle);
    float sin_a   = sinf(angle);
    float cos_a90 = cosf(angle + (float)M_PI / 2.0f);
    float sin_a90 = sinf(angle + (float)M_PI / 2.0f);

    mb.vertex(ax + cos_a   * arrow_size,            ay + sin_a   * arrow_size);
    mb.vertex(ax + cos_a90 * (arrow_size * 0.5f),   ay + sin_a90 * (arrow_size * 0.5f));
    mb.vertex(ax - cos_a90 * (arrow_size * 0.5f),   ay - sin_a90 * (arrow_size * 0.5f));
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

// ONE full-window pass, not per-viewport: there is one pause state and one
// shared cursor, so drawing the menu in every split-screen cell put four
// identical menus on screen, each answering the same keys (4P field bug).
// Called from GLGame::draw after the viewport passes, with the same
// full-window viewport/ortho recipe as net_overlays.
void Overlay::paused(const GLGame *glgame) {
  // Once the game is over, the game-over messaging (the shared GAME OVER
  // card online / in replays, the per-ship indicator offline) owns the
  // centre of the screen — never stack "Paused" under it. toggle_pause
  // refuses new pauses on a finished game; this covers a game that ENDS
  // while already paused (e.g. the host leaving a paused game is terminal
  // for a spectating client).
  if (glgame->all_players_out()) return;
  if (glgame->running) return;
  // The roster replaces this screen entirely (seat_roster draws its own
  // dim): "Paused" bled through it mid-list on both layouts, and the
  // touch hint named a button the roster does not answer.
  if (glgame->roster_open()) return;
  // A help card is the thing that paused the game and already fills its
  // owner's viewport; drawing "Paused" across the window would stack over
  // it. pause_menu_active() refuses while ANY player's help is open (one
  // predicate, both sides — the old per-viewport form tested only the
  // viewport's own ship and drew a RESUME cursor that answered nothing);
  // the title follows the same any-player rule.
  for (const GLShip *gs : *glgame->players)
    if (gs->show_help) return;

  glViewport(0, 0, glgame->window.x(), glgame->window.y());
  float hw = glgame->window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = glgame->window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  // Dim the whole frame while the cursor menu is live: in a 4P split the
  // menu sat over four viewports of HUD text and starfield and was hard to
  // pick out (field feedback). Exactly the menu case — the touch pause
  // screen keeps its play button bright (it IS the resume control), and a
  // replay's plain "Paused" title leaves the timeline chrome undimmed.
  bool menu_live = glgame->pause_menu_active();
  if (menu_live) {
    static MeshBuilder mb;
    static Mesh mesh;
    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(0.0f, 0.0f, 0.0f, 0.6f);
    mb.vertex(-hw, -hh); mb.vertex(hw, -hh); mb.vertex(hw, hh);
    mb.vertex(-hw, -hh); mb.vertex(hw, hh); mb.vertex(-hw, hh);
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }

  Typer::draw_centered(0, 30, "Paused", 25);
  if(is_touch_mode()) {
    // Touch has no cursor on any screen: the pause button resumes and the
    // EXIT TO MENU band below leaves.
    Typer::draw_centered(0, -40, "press play to resume", 8);
    return;
  }
  // Selectable rows carrying the shared menu cursor, sized so the stack
  // (size 13 rows at -42 and -80) bottoms out around -106, clear of the
  // disconnect card's row at y=-130. The two no longer show at once —
  // pause_menu_active() refuses while that card owns input — but the
  // positions stay put so neither screen shifts.
  if (!menu_live) return;
  // Two rows online (a peer's seat isn't ours to rearrange), three offline
  // with PLAYERS between them — the row list and the ladder both come from
  // pause_row_count/pause_row_at, so they can't disagree about what row 1 is.
  int rows = glgame->pause_row_count();
  for (int i = 0; i < rows; i++) {
    const char *label = "EXIT TO MENU";
    switch (glgame->pause_row_at(i)) {
      case GLGame::PAUSE_RESUME:  label = "RESUME"; break;
      case GLGame::PAUSE_PLAYERS: label = "PLAYERS"; break;
      default: break;
    }
    MenuSelect::draw_row(PAUSE_ROW_Y0 - i * PAUSE_ROW_GAP, label, PAUSE_ROW_SZ,
                         glgame->pause_selection_ == i);
  }
}

// The seat roster (offline): one row per seat showing what drives it, plus
// an ADD row while there is space. Full-window like the pause menu it sits
// over — there is one roster, not one per split-screen cell.
void Overlay::seat_roster(const GLGame *glgame) {
  if (!glgame->roster_open()) return;

  glViewport(0, 0, glgame->window.x(), glgame->window.y());
  float hw = glgame->window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = glgame->window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  {  // same dim as the pause menu — this screen replaces it
    static MeshBuilder mb;
    static Mesh mesh;
    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(0.0f, 0.0f, 0.0f, 0.6f);
    mb.vertex(-hw, -hh); mb.vertex(hw, -hh); mb.vertex(hw, hh);
    mb.vertex(-hw, -hh); mb.vertex(hw, hh); mb.vertex(-hw, hh);
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }

  if (is_touch_mode()) {
    // Touch host roster (FOURPLAYER.md O3, touch pass): peer blocks with
    // finger-sized action zones — geometry from TapBand::roster_*, the
    // lobby manage view's, so the two surfaces read identically. Only
    // remote pilots are listed: input re-binding is a cursor idea, and
    // roster_available gates touch to the online host's kick UI. Armed
    // zones ride roster_kick_armed_/roster_ban_, and GLGame::touch_tap
    // answers the same bands this draws.
    Typer::draw_centered(0, 340, "PLAYERS", 20);
    int prows = (int)glgame->players->size();
    int block = 0;
    for (int i = 0; i < prows && block < TapBand::ROSTER_BLOCKS; i++) {
      if (!glgame->roster_row_is_peer(i)) continue;
      const GLGame::NetPeer *p =
          const_cast<GLGame *>(glgame)->roster_peer_at(i);
      std::string who = p ? net_identity_name_or(p->identity, "",
                                                 glgame->net_id_ctx())
                          : std::string();
      char label[48];
      if (who.empty())
        snprintf(label, sizeof label, "PLAYER %d", i + 1);
      else
        snprintf(label, sizeof label, "PLAYER %d - %s", i + 1, who.c_str());
      Typer::draw_centered(0, TapBand::roster_row_y(block), label, 15);
      bool split = glgame->roster_row_can_ban(i);
      bool armed = glgame->roster_kick_armed_ == i;
      TapBand::roster_action(block, false, split)
          .draw(armed && !glgame->roster_ban_ ? "CONFIRM KICK" : "KICK");
      if (split)
        TapBand::roster_action(block, true, split)
            .draw(armed && glgame->roster_ban_ ? "CONFIRM BAN" : "BAN");
      block++;
    }
    {
      std::string anon = "ALLOW ANONYMOUS PLAYERS   ";
      anon += g_prefs.allow_anonymous ? "YES" : "NO";
      TapBand::roster_anon.draw(anon.c_str());
    }
    // The way back to the pause screen, on the exit band's spot — the
    // label says which level it pops (GLGame::touch_tap answers it).
    glgame->exit_band().draw(Typer::cursored("BACK", true).c_str(),
                             glgame->current_time);
    return;
  }

  bool hosting = glgame->net_mode_ == GLGame::NetHost;
  Typer::draw_centered(0, 150, "PLAYERS", 20);
  int rows = glgame->roster_row_count();
  int seats = (int)glgame->players->size();
  for (int i = 0; i < rows; i++) {
    char label[80];
    if (glgame->roster_row_is_exit(i)) {
      // The way out, as a row — see GLGame::roster_row_is_exit.
      snprintf(label, sizeof label, "BACK");
    } else if (glgame->roster_row_is_anon(i)) {
      // Who may take the seats that are still empty. Checked before the
      // ADD row: online there is no ADD row, and this one sits where it
      // would have been.
      snprintf(label, sizeof label, "ALLOW ANONYMOUS PLAYERS   %s",
               g_prefs.allow_anonymous ? "YES" : "NO");
    } else if (i >= seats) {
      snprintf(label, sizeof label, "ADD PLAYER %d", i + 1);
    } else if (glgame->roster_row_is_peer(i)) {
      // A remote pilot: name them (their badge, else the role label) and
      // say what confirm will do — armed rows say it twice as loudly,
      // because the next press ends their game.
      // No fallback name here: the row ALREADY opens with "PLAYER n", and
      // the unnamed-pilot fallback is that same role label — so an
      // unattested peer (the common online case) read "PLAYER 2   PLAYER 2"
      // (field, 2026-08-13). An empty name just leaves the seat label
      // standing on its own, which is all it was ever saying.
      const GLGame::NetPeer *p =
          const_cast<GLGame *>(glgame)->roster_peer_at(i);
      std::string who = p ? net_identity_name_or(p->identity, "",
                                                 glgame->net_id_ctx())
                          : std::string();
      // The action this row is offering, with the arrows that swap it —
      // one row, two actions (kick, which they can come back from, and
      // ban, which they can't), and which one is armed is never in doubt.
      // Kept short: the row already carries a name of up to 24 glyphs.
      // BAN is only on offer where a ban would hold (a worker-attested
      // name): elsewhere the row says KICK alone, with no arrows, because
      // there is no second action to swap to.
      bool can_ban = glgame->roster_row_can_ban(i);
      const char *act = glgame->roster_ban_ ? "BAN" : "KICK";
      char action[24];
      if (glgame->roster_selection_ != i)
        // Not the cursor's row: name the actions without claiming either is
        // picked. roster_ban_ belongs to the HIGHLIGHTED row, so rendering
        // it here put "< BAN >" on rows nobody had chosen.
        snprintf(action, sizeof action, can_ban ? "KICK / BAN" : "KICK");
      else if (glgame->roster_kick_armed_ == i)
        snprintf(action, sizeof action, "[CONFIRM %s]", act);
      else if (can_ban)
        snprintf(action, sizeof action, "< %s >", act);
      else
        snprintf(action, sizeof action, "KICK");
      if (who.empty())
        snprintf(label, sizeof label, "PLAYER %d   %s", i + 1, action);
      else
        snprintf(label, sizeof label, "PLAYER %d   %s   %s", i + 1,
                 who.c_str(), action);
    } else {
      snprintf(label, sizeof label, "PLAYER %d   %s", i + 1,
               glgame->roster_seat_label(i).c_str());
    }
    MenuSelect::draw_row(90 - i * 44, label, 13, glgame->roster_selection_ == i);
  }
  // Online rows carry their own grammar and get NO footer: "< KICK >" shows
  // the arrows that swap the action and "[CONFIRM ...]" shows the press that
  // performs it, so a line underneath spelling out the keys was a third
  // statement of what two labels already said. The offline keeps its two —
  // input cycling and press-to-claim have nothing on the row to show them.
  if (!hosting) {
    // Anchored under the LAST row, not at a fixed y: the list grew by the
    // exit row, and a fixed anchor would print these through it at four
    // seats. One definition of the row pitch, used by both.
    float below = 90 - (rows - 1) * 44 - 45;
    Typer::draw_centered(0, below, "LEFT / RIGHT   CHANGE INPUT", 8);
    Typer::draw_centered(0, below - 25, "UNUSED PAD: PRESS ANY BUTTON TO TAKE THIS SEAT", 8);
  }
  // (No "ESC BACK" line: the exit is the last ROW now — a key spelled out
  // under a list whose every other row is picked with the cursor was the
  // one way out the cursor could not reach. Esc still steps back.)
}


void Overlay::level(const GLGame *glgame, const GLShip *glship) {
  char buf[20];
  snprintf(buf, sizeof(buf), "LEVEL %d", glgame->generation + 1);
  Typer::draw_centered(0, top_hud_y(glgame), buf, 12 * hud_fit(glgame));
}

// Is the always-on room line on screen? Shared by the banner itself and by
// the indicators that have to keep out of its way, so the two can't disagree
// about whether the space above them is taken.
bool Overlay::room_line_shown(const GLGame *glgame) {
  return !glgame->net_room_code_.empty() &&
         glgame->net_mode_ != GLGame::NetOff &&
         glgame->net_mode_ != GLGame::NetReplay &&
         !glgame->all_players_out();
}

// How far the centre-column countdowns (god mode, time slow) start BELOW
// their usual top-anchored spot, so they clear the room line.
//
// The room line is drawn ONCE over the whole window, centred, while these
// hang off each VIEWPORT's own top — two coordinate spaces that only collide
// where a viewport owns the window's centre column. That is every
// single-column layout: 1P, and the portrait 2P strips. With two columns the
// line sits on the seam, ~180 units of text against a 400-unit half
// viewport, so it never reaches either cell's centre and nothing moves.
//
// Field report: online 1P, where the time-slow label drew straight through
// "ROOM XXXXX" and the god-mode seconds clipped its top.
float Overlay::room_line_drop(const GLGame *glgame) {
  if (!room_line_shown(glgame) || glgame->num_x_viewports() > 1) return 0.0f;
  float H = Typer::scaled_window_height;       // full-window half-height
  int ny = glgame->num_y_viewports();
  float line_y = ROOM_LINE_Y * H;              // in full-window coords
  // Which viewport row the line lands in, and where that row's centre is:
  // row r spans full-y [H - 2H(r+1)/ny, H - 2H r/ny]. Subtracting the centre
  // converts the line into that viewport's own coords. Both strips get the
  // same drop — one shared banner, one shared layout.
  int row = (int)((H - line_y) * ny / (2.0f * H));
  if (row < 0) row = 0;
  if (row > ny - 1) row = ny - 1;
  float row_centre = H - H * (2 * row + 1) / ny;
  float bottom = (line_y - row_centre) - 2.0f * ROOM_LINE_SZ;  // ink's floor
  float f = hud_fit(glgame);
  float first = top_hud_y(glgame) - 62 * f;    // the god-mode label's anchor
  float drop = first - (bottom - 10.0f);       // ...plus a gap
  return drop > 0.0f ? drop : 0.0f;
}

// Where the centre "banner spot" text goes — the generation banner and the
// partial-loss header, one at a time (the banner slot is single and the
// partial-loss notice only draws with no banner up). Normally vh*0.55,
// proven for live gameplay text and clear of the top-anchored countdowns.
// But the room_line_drop above lands the countdown stack exactly in that
// band — the dropped god-mode seconds sit INSIDE the banner's ink — so
// while the drop is active the spot moves up to the header spot instead
// (vh*0.80, the JOINED/DISCONNECTED headers' position, above the room
// line), which is free in both states for as long as this text shows.
// Asking room_line_drop keeps this the countdowns' own condition: the two
// can't disagree about who owns the band.
float Overlay::banner_spot_y(const GLGame *glgame) {
  float vh = Typer::scaled_window_height;
  return room_line_drop(glgame) > 0.0f ? vh * 0.80f : vh * 0.55f;
}

void Overlay::god_mode(const GLGame *glgame, const GLShip *glship) {
  int remaining = glship->ship->god_mode_time_remaining();
  if(remaining <= 0) return;
  float f = hud_fit(glgame);
  // One drop for the whole stack, not a per-line nudge: shifting the block
  // as a unit keeps the god-mode/time-slow spacing that stops THOSE two
  // overlapping when both run at once.
  float base_y = top_hud_y(glgame) - room_line_drop(glgame);
  Typer::draw_centered(0, base_y - 62 * f, "God mode", 10 * f);
  Typer::draw_centered(0, base_y - 100 * f, remaining / 1000, 10 * f);
}

// The time-slow pickup's countdown, in WALL seconds (the number the player
// experiences). Drawn a block below the god-mode indicator so the two never
// overlap when both are running.
void Overlay::time_slow(const GLGame *glgame, const GLShip *glship) {
  (void)glship;  // a world effect: every viewport shows the same countdown
  int remaining = glgame->time_slow_wall_ms_remaining();
  if(remaining <= 0) return;
  float f = hud_fit(glgame);
  float base_y = top_hud_y(glgame) - room_line_drop(glgame);
  Typer::draw_centered(0, base_y - 137 * f, "Time slow", 10 * f);
  Typer::draw_centered(0, base_y - 175 * f, (remaining + 999) / 1000, 10 * f);
}

void Overlay::score(const GLGame *glgame, const GLShip *glship) {
  float f = hud_fit(glgame);
  float vw = Typer::scaled_window_width / glgame->num_x_viewports();
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  float top_y = top_hud_y(glgame);
  float drop = (vh - 20 - CORNER_INSET) - top_y;  // cutout shift, 0 without one
  Typer::draw(vw - 40 * f - CORNER_INSET, top_y, glship->ship->score, 20 * f);
  if(glship->ship->multiplier() > 1) {
    Typer::draw(vw - 35 * f - CORNER_INSET, vh - (92 * f) - CORNER_INSET - drop, "x", 15 * f);
    Typer::draw(vw - 65 * f - CORNER_INSET, vh - (80 * f) - CORNER_INSET - drop, glship->ship->multiplier(), 20 * f);
  }
}

void Overlay::level_cleared(const GLGame *glgame, const GLShip *glship) {
  // The connection-lost card owns the centre of the screen, and this
  // countdown is going nowhere without the host — CLEARED at y=150 sat
  // right under the card's heading at y=160. The host-with-a-door notice
  // keeps it: that game (and its countdown) really is still running.
  if (glgame->net_card_owns_input()) return;
  if(glgame->running && glgame->level_cleared && (glship->ship->is_alive() || glship->ship->lives > 0)) {
    int countdown = (glgame->time_until_next_generation / 1000) + 1;
    if (viewport_is_grid_cell(glgame)) {
      // A quarter viewport has no free width beside the weapons list: at
      // the classic anchor the banner ran straight through "MISSILES 994 /
      // FIRE X NEXT C" (field bug). The list is at most four rows (the
      // current primary + secondary), so dropping the banner just below it
      // clears the column outright — better than shrinking the text until
      // it fits between the columns, which took it near unreadable.
      Typer::draw_centered(0, -35, "CLEARED", 50 * hud_fit(glgame));
      Typer::draw_centered(0, -150, countdown, 20 * hud_fit(glgame));
    } else {
      Typer::draw_centered(0, 150, "CLEARED", 50);
      Typer::draw_centered(0, -60, countdown, 20);
    }
  }
}

void Overlay::lives(const GLGame *glgame, const GLShip *glship) {
  Typer::draw_lives(Typer::scaled_window_width/glgame->num_x_viewports()-40-CORNER_INSET, -Typer::scaled_window_height/glgame->num_y_viewports()+70+CORNER_INSET, glship, 18);
}

void Overlay::weapons(const GLGame *glgame, const GLShip *glship) {
  float saved[16]; gles2_get_mvp(saved);
  float s = Typer::scale;
  float vp[16]; mat4_translate(vp, saved,
    (-Typer::scaled_window_width/glgame->num_x_viewports()+CORNER_INSET) * s,
    (Typer::scaled_window_height/glgame->num_y_viewports()-CORNER_INSET) * s
        - safe_inset_top() * 2.0f, 0.0f);
  gles2_set_vp(vp);
  glship->draw_weapons();
  gles2_set_vp(saved);
}

void Overlay::temperature(const GLGame *glgame, const GLShip *glship) {
  float base[16]; gles2_get_mvp(base);
  float tx = -Typer::scaled_window_width/glgame->num_x_viewports()+30+CORNER_INSET;
  float ty = -Typer::scaled_window_height/glgame->num_y_viewports()+15+CORNER_INSET;
  float inner[16]; mat4_translate(inner, base, tx, ty, 0.0f);
  float temp_vp[16]; mat4_scale(temp_vp, inner, 30.0f, 30.0f, 1.0f);
  gles2_set_vp(temp_vp);
  glship->draw_temperature();
  float status_vp[16]; mat4_translate(status_vp, inner, 42.0f, 147.0f, 0.0f);
  mat4_scale(status_vp, status_vp, 10.0f, 10.0f, 1.0f);
  gles2_set_vp(status_vp);
  glship->draw_temperature_status();
  gles2_set_vp(base);
}

void Overlay::respawn_timer(const GLGame *glgame, const GLShip *glship) {
  // draw_respawn_timer()'s dead-with-no-lives branch renders its own
  // "GameOver" + score. Online that messaging is owned by the full-screen
  // overlays — the shared GAME OVER card (net_overlays) and, before that, the
  // SPECTATING countdown — so the per-ship indicator would just double them
  // up. Suppress it whenever the drawn ship is fully out online; the respawn
  // countdown (lives > 0) and the offline split-screen indicator still show.
  // Also suppress it OFFLINE when a leaderboard flow is live — board_prompt
  // then owns the whole game-over card (heading included), so this per-ship
  // "GameOver" would collide with it.
  if(!glship->ship->is_alive() && glship->ship->lives <= 0 &&
     (glgame->net_active() || glgame->board_phase_ != GLGame::BoardOff))
    return;
  if(glgame->running && !glship->show_help) {
    float saved[16]; gles2_get_mvp(saved);
    float vp[16]; mat4_scale(vp, saved, 20.0f, 20.0f, 1.0f);
    gles2_set_vp(vp);
    glship->draw_respawn_timer();
    gles2_set_vp(saved);
  }
}

void Overlay::net_badges(const GLGame *glgame, const GLShip *glship) {
  (void)glship;
  if (!glgame->net_active()) return;
  // Bottom rows like the SPECTATING hint, clear of the touch EXIT TO MENU
  // band and the title-safe margin; hoisted whenever that band is up —
  // spectating (the camera is on the peer, exactly when the tag matters
  // most), but also the touch pause / game-over / connection-lost states:
  // the band's landscape label ink runs -420..-446 and the LOCAL row at
  // vhb+168 (-432..-454) would print straight through it. The hoist must
  // clear the WHOLE band, not just its label: the band's glyph box tops
  // out ~-370 (anchor -420, size 13, pad 50, to_bottom below), and the old
  // +175 spot printed the badge exactly on the band's EXIT TO MENU text
  // (field: iPhone, 2026-07-24). +255 sits above the band's reach with air
  // to spare (portrait's vhb+215 re-anchor stays clear too).
  float vhb = -Typer::scaled_window_height / glgame->num_y_viewports();
  bool hoist = glgame->is_spectating() || glgame->exit_band_showing();
  float y = hoist ? vhb + 255.0f : vhb + 130.0f;
  // NetReplay: net_active() is true but the ghosts may be anyone's (a
  // downloaded run) — keep the old single badge row when an identity is
  // known, and no fallback rows.
  if (glgame->net_mode_ != GLGame::NetHost &&
      glgame->net_mode_ != GLGame::NetClient) {
    std::string badge = net_identity_badge_or(
        glgame->net_peer_identity(), glgame->net_peer_fallback().c_str(),
        glgame->net_id_ctx());
    if (badge.empty()) return;
    const GLShip *peer = glgame->remote_player();
    char peer_score[16];
    const char *peer_suffix = nullptr;
    if (peer) {
      snprintf(peer_score, sizeof peer_score, ": %d", peer->ship->score);
      peer_suffix = peer_score;
    }
    Typer::draw_centered_verified(
        0, y, badge.c_str(), 11,
        net_identity_verified(glgame->net_peer_identity(),
                              glgame->net_id_ctx()),
        0, peer_suffix);
    return;
  }
  // Live play: one row per occupied seat, P1 on top — the SAME order on
  // every machine (the pre-4P layout put "you" on top, which reads fine
  // at two rows but scrambles at four when each machine reshuffles).
  // Each row carries its pilot's live score (": 4200") — every score is
  // already on this machine with no wire cost: the host sims and credits
  // all ships authoritatively, and the client's 10 Hz snapshot restores
  // every player's score (net_apply_state). The score rides as
  // draw_centered_verified's SUFFIX so the verified tick stays beside the
  // identity it vouches for, never after the score.
  // Remote-row identity: net_identity_for_seat (the host's roster / the
  // client's MSG_PEER_IDENT store; the client's seat-1 row is the
  // handshake identity). A row with nothing renderable — a legacy peer,
  // or an online peer whose attestation never arrived — falls back to the
  // bare role label rather than vanishing: the score is always known
  // locally, and a scoreless blank row read as a bug in the field
  // (Android/Steam pairing, 2026-08-07).
  // The local row draws no verified tick: the tick vouches to YOU about a
  // PEER (the worker attests each side to the OTHER, never back to its
  // claimant), and your own machine needs no vouching about itself.
  // Rows stack upward from y in 38-unit steps (glyphs descend ~2x size
  // below their y, so 38 clears a size-11 row with a visible gap — 30
  // read as almost touching in the field); at two players this is the
  // exact pre-4P geometry.
  int local_seat = glgame->net_local_seat();
  int rows = 0;
  for (int seat = 1; seat <= MAX_PLAYERS; seat++)
    if (glgame->player_by_seat(seat)) rows++;
  float row_y = y + 38.0f * (float)(rows - 1);
  for (int seat = 1; seat <= MAX_PLAYERS; seat++) {
    const GLShip *gs = glgame->player_by_seat(seat);
    if (!gs) continue;
    char score[16];
    snprintf(score, sizeof score, ": %d", gs->ship->score);
    if (seat == local_seat) {
      std::string self =
          net_local_identity_badge(glgame->net_local_fallback().c_str());
      Typer::draw_centered_verified(0, row_y, self.c_str(), 11, false, 0,
                                    score);
    } else {
      // Seat 1 on a client keeps the LAN device-name nicety
      // (net_peer_fallback); other seats get their role label.
      // A client's seats-2..4 tick (and the ATTESTED trust that lets the
      // name render at all) is the HOST's assertion relayed via
      // MSG_PEER_IDENT — a weaker vouch than seat 1's direct worker
      // attestation. Accepted deliberately (net_protocol.h): the host is
      // sim-authoritative for far more than a name, and the receiver
      // clamps unknown trust values down, never up.
      std::string fallback = seat == 1 ? glgame->net_peer_fallback()
                                       : "PLAYER " + std::to_string(seat);
      // Per-seat context: a LAN-door peer's claimed name renders even in
      // a room that also holds relay peers (net_id_ctx_for_seat).
      NetIdentityCtx ctx = glgame->net_id_ctx_for_seat(seat);
      const NetIdentity &id = glgame->net_identity_for_seat(seat);
      std::string badge = net_identity_badge_or(id, fallback.c_str(), ctx);
      if (badge.empty()) badge = fallback;
      Typer::draw_centered_verified(0, row_y, badge.c_str(), 11,
                                    net_identity_verified(id, ctx), 0, score);
    }
    row_y -= 38.0f;
  }
}

// Spectator flow (netplay co-op): a "SPECTATING IN N" countdown on the local
// wreck, then "SPECTATING" at the bottom once the camera has handed off to the
// peer. Both phases are driven by GLGame::spectate_death_time_.
void Overlay::spectate(const GLGame *glgame, const GLShip *glship) {
  (void)glship;
  if (glgame->spectate_arming()) {
    // The local player IS game over (out of lives) — say so, with the
    // hand-off countdown below it.
    char buf[24];
    snprintf(buf, sizeof buf, "SPECTATING IN %d", glgame->spectate_countdown_secs());
    Typer::draw_centered(0, 120, "GAME OVER", 30);
    Typer::draw_centered(0, 20, buf, 18);
  } else if (glgame->is_spectating()) {
    // Bottom of the viewport, clear of the touch EXIT TO MENU band.
    float vhb = -Typer::scaled_window_height / glgame->num_y_viewports();
    Typer::draw_centered(0, vhb + 130, "SPECTATING", 16);
  }
}

void Overlay::board_prompt(const GLGame *glgame) {
  // No board flow, or the query is still silent (BoardQualifying draws
  // nothing — the card must never look like it is waiting on the network):
  // the ordinary GAME OVER cards own the screen.
  if (glgame->board_phase_ == GLGame::BoardOff ||
      glgame->board_phase_ == GLGame::BoardQualifying)
    return;

  // From here on this overlay OWNS the whole game-over card in every mode
  // (net_overlays and the offline respawn/title cards all stand down while
  // board_phase_ is active), so it draws its own GAME OVER heading and its
  // own EXIT TO MENU row — everything on ONE spaced layout, no collision
  // with the per-mode cards.
  glViewport(0, 0, glgame->window.x(), glgame->window.y());
  float hw = glgame->window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = glgame->window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);
  bool touch = is_touch_mode();

  // The score stays front and centre through EVERY phase — the upload is
  // about the score, so no state may hide it. Topmost, above the heading
  // (glyphs descend ~2x size below their y: 218 - 40 = 178 stays clear of
  // GAME OVER's 150, which itself descends to 90 clear of the phase rows).
  {
    char score_line[32];
    snprintf(score_line, sizeof(score_line), "SCORE %u", glgame->board_score_);
    Typer::draw_centered(0, 218, score_line, 20);
  }
  Typer::draw_centered(0, 150, "GAME OVER", 30);

  if (glgame->board_phase_ == GLGame::BoardPrompt) {
    char line[64];
    snprintf(line, sizeof(line), "WOULD PLACE #%d THIS SEASON",
             glgame->board_place_);
    Typer::draw_centered(0, 60, line, 14);
    Typer::draw_centered(0, 20, "UPLOAD TO LEADERBOARD?", 16);
    if (touch) {
      // YES left half, NO right half (the New-game confirm's grammar). The
      // exit band underneath still exits — GLGame::touch_tap carves it out.
      Typer::draw_centered(-150, -50, "YES", 22);
      Typer::draw_centered(150, -50, "NO", 22);
    } else {
      MenuSelect::draw_row(-40, "YES", 16, glgame->board_yes_);
      MenuSelect::draw_row(-90, "NO", 16, !glgame->board_yes_);
    }
    return;
  }
  if (glgame->board_phase_ == GLGame::BoardUploading) {
    int pct = glgame->board_ ? glgame->board_->transfer_pct() : -1;
    char line[48];
    if (pct >= 0) snprintf(line, sizeof(line), "UPLOADING %d%%", pct);
    else          snprintf(line, sizeof(line), "UPLOADING");
    Typer::draw_centered(0, 20, line, 18);
    // No keyboard Esc on touch: label the exit band's cancel affordance.
    Typer::draw_centered(0, -40, touch ? "LEAVE TO CANCEL" : "ESC TO CANCEL",
                         11);
    return;
  }
  // Terminal phases: a result line, then the EXIT TO MENU row (this
  // overlay owns it now — the per-mode cards have stood down).
  if (glgame->board_phase_ == GLGame::BoardPlaced) {
    char line[48];
    snprintf(line, sizeof(line), "UPLOADED - RANK #%d", glgame->board_place_);
    Typer::draw_centered(0, 40, line, 16);
  } else if (glgame->board_phase_ == GLGame::BoardFailed) {
    // Map the (sanitized) worker reason to a player-facing line; benign
    // refusals are not "failures".
    const std::string &r = glgame->board_fail_reason_;
    const char *msg = "UPLOAD FAILED";
    if (r == "not-best" || r == "already-submitted")
      msg = "ALREADY ON THE BOARD";
    else if (r == "unverified") msg = "UPLOAD FAILED - NOT VERIFIED";
    else if (r == "bad-season") msg = "SEASON NOT ACCEPTED";
    else if (r == "rate-limited") msg = "UPLOAD FAILED - TRY LATER";
    else if (r == "connection") msg = "UPLOAD FAILED - CONNECTION";
    Typer::draw_centered(0, 40, msg, 14);
  }
  // Touch already has the EXIT TO MENU tap band under the card (title_text
  // draws it whenever the game is over); desktop/controller get the row.
  if (!touch)
    MenuSelect::draw_row(-60, "EXIT TO MENU", 16, true);
}

// Human-readable name of a bound key for HUD hints (F-keys arrive as
// 128 + GLUT function-key code; everything else is the character itself).
static void key_hint(int key, char *out, size_t n, const char *verb) {
  if (key > 128 && key <= 128 + 12)
    snprintf(out, n, "%s controls with F%d", verb, key - 128);
  else if (key >= 33 && key <= 126)
    snprintf(out, n, "%s controls with %c", verb, (char)key);
  else
    snprintf(out, n, "%s controls with F1", verb);
}

void Overlay::keymap(const GLGame *glgame, const GLShip *glship) {
  if(glship->show_help) {
    // Dim THIS viewport under the card (the pause dim's per-viewport twin;
    // field: "too many menu elements" behind the card). The quad is
    // deliberately oversized — the viewport transform clips it to exactly
    // this player's screen, so no per-layout extents are needed, and other
    // players' viewports stay bright.
    {
      static MeshBuilder mb;
      static Mesh mesh;
      const float E = 100000.0f;
      mb.clear();
      mb.begin(GL_TRIANGLES);
      mb.color(0.0f, 0.0f, 0.0f, 0.6f);
      mb.vertex(-E, -E); mb.vertex(E, -E); mb.vertex(E, E);
      mb.vertex(-E, -E); mb.vertex(E, E);  mb.vertex(-E, E);
      mb.end();
      mesh.upload(mb, GL_DYNAMIC_DRAW);
      mesh.draw();
    }
    // The card is laid out for a full-height viewport: it reaches ~485
    // virtual units above centre, while a 2x2 grid cell only has
    // scaled_window_height / ny (300 at ny=2) — the top half clipped off
    // the quarter screens (4P field bug). Scale it to what the viewport
    // can actually show; full-height viewports keep the classic size.
    float avail = Typer::scaled_window_height / glgame->num_y_viewports();
    float fit = avail / 500.0f;
    if (fit > 1.0f) fit = 1.0f;
    glship->draw_keymap(fit);
    // The way OUT stays full-brightness: title_text draws only the "show"
    // variant (it runs before the dim), the "hide" hint is drawn here on
    // top of it, at the same spot title_text would have used.
    if(!glship->last_input_was_controller && !is_touch_mode() &&
       glship->help_key.primary() != 0) {
      char hint[48];
      key_hint(glship->help_key.primary(), hint, sizeof(hint), "hide");
      // Exactly where title_text draws the "show" twin, in every layout —
      // the hint must not jump across the screen when the card opens.
      float vh = Typer::scaled_window_height / glgame->num_y_viewports();
      Typer::draw_centered(0, -vh + 85, hint, 8);
    }
  }
}


void Overlay::title_text(const GLGame *glgame, const GLShip *glship) {
  Ship* p1 = glgame->players->front()->ship;
  // Per VIEWPORT, not per window. The join and controls hints were the only
  // overlay elements that didn't divide by the viewport counts: they sat at
  // the window's half-width — which IS the viewport's edge once the screen
  // splits, so "player 3 press start to join" was drawn half off the side of
  // every strip (field, 2026-08-11) — anchored to the window's height, which
  // on a 2x2 grid is above the cell entirely.
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  if((int)glgame->players->size() < LOCAL_PLAYER_CAP) {
    // -40 (not -10): a real margin inside the title-safe edge, matching the
    // bottom-row hints (Xbox compliance) — pulled down further by the
    // cutout inset so this row stays aligned with the LEVEL/score/weapons
    // row below the camera notch.
    float top_y = vh - 40 - safe_inset_top_v();
    if(p1->is_alive() || p1->lives > 0) {
      // The join invitation blinks — it is an offer, not the only move here.
      if((glgame->current_time/1400) % 2 && !is_touch_mode()) {
        int next_seat = (int)glgame->players->size() + 1;
        char join_hint[40];
        if(glgame->has_free_controller()) {
          snprintf(join_hint, sizeof(join_hint),
                   "player %d press start to join", next_seat);
          Typer::draw_centered(0, top_y, join_hint, 8);
        }
#ifndef _GAMING_XBOX
        // Keyboard join hint — on Xbox the only join path is a second
        // controller, and Enter only ever joins the P2 seat (FOURPLAYER.md
        // D3: P3/P4 are controller-first).
        else if(!is_steam_gamemode() && next_seat == 2)
          Typer::draw_centered(0, top_y, "player 2 press enter to join", 8);
#endif
      }
    } else {
      // Game over: the same row, at the same size, that the online GAME
      // OVER card and the disconnect card draw — one exit affordance across
      // every end-state instead of a hint naming a different key per
      // screen, and steady like those, because it IS the move here.
      // BELOW the ending like the online card (which stacks GAME OVER at
      // 60 over the row at -80): draw_respawn_timer's 20x viewport puts
      // "GameOver" at y 80..0 and the score at -20..-60 in this space, so
      // -100 clears both. Suppressed while a leaderboard flow owns the
      // card (board_prompt draws its own heading + EXIT TO MENU row).
      // Touch has no row — the tap band at the screen edge is the exit.
      if (!is_touch_mode() && glgame->board_phase_ == GLGame::BoardOff)
        MenuSelect::draw_row(-100, "EXIT TO MENU", 16, true);
      // The upload prompt this card would carry can never appear with
      // recording off — there is nothing to submit — and the silence read
      // as a bug in the field (LEADERBOARD.md L7): say why, once the whole
      // game is over (P1's wreck alone must not nag a live P2 run). In the
      // lowercase hint register ("show controls with F1"), one small line
      // — an aside, not part of the card — below the exit row (-100
      // descends to -132), clear of the touch band's reach (~-370 up).
      if (glgame->board_phase_ == GLGame::BoardOff &&
          glgame->all_players_out() && !Replay::recording_enabled() &&
          net_board_can_submit())
        Typer::draw_centered(0, -170,
                             "turn on record replays to enter scores",
                             is_touch_mode() ? 10 : 8);
    }
  } else {
    // Keep these clear of the bottom edge: Typer glyphs extend ~2x the
    // size below their anchor, so a small offset puts the text on the
    // title-safe boundary (an Xbox-compliance problem, and clipped-looking
    // on desktop where the safe area IS the screen edge).
    if(glgame->friendly_fire) {
      glgame->ff_toggle_band().draw("friendly fire on");
    } else if(is_touch_mode()) {
      // Touch: the text doubles as the toggle region (GLGame::touch_tap
      // hit-tests the same band), so the OFF state must be visible too or
      // it can't be turned back on.
      glgame->ff_toggle_band().draw("friendly fire off");
    }
  }
  // The show-controls hint, centred at the bottom of the viewport in EVERY
  // layout — the placement the 3-4P branch always used, now shared. It used
  // to sit on the top row opposite the join hint, which only fits a
  // full-width viewport: on a split, two strings this long cannot share
  // that row (a keyboard player with a spare pad connected sees both at
  // once), so one had to move rather than both crowd the centre.
  // A seat with NO help binding (pad-joined P3/P4) gets no hint at all:
  // key_hint's fallback would name P1's F1, which does nothing for that
  // seat — its card is on R3, and the card itself says so. The "hide"
  // variant is drawn by keymap() so it sits above the card's dim.
  if(!glship->last_input_was_controller && !is_touch_mode() &&
     glship->help_key.primary() != 0 && !glship->show_help &&
     (glgame->current_time)/12000 % 2) {
    char hint[48];
    key_hint(glship->help_key.primary(), hint, sizeof(hint), "show");
    Typer::draw_centered(0, -vh + 85, hint, 8);
  }
  // Boost discoverability (first-use hint): boost is the least-found
  // control in the game, so until the pilot has boosted ONCE (ever —
  // Preferences::boost_hint_done, latched by GLShip on any boost) the
  // same bottom line names it on the help hint's ALTERNATE flash phase,
  // so the two hints trade places instead of stacking. Keyboard names the
  // binding; a pad pilot gets the bumper. Touch has its own button.
  if(!is_touch_mode() && !glship->show_help && !g_prefs.boost_hint_done &&
     !((glgame->current_time)/12000 % 2)) {
    char hint[48];
    if(glship->last_input_was_controller) {
      snprintf(hint, sizeof(hint), "boost with left bumper");
    } else {
      int bk = glship->boost_key.primary();
      if(bk >= 33 && bk <= 126)
        snprintf(hint, sizeof(hint), "boost with %c", (char)bk);
      else
        hint[0] = '\0';
    }
    if(hint[0] != '\0')
      Typer::draw_centered(0, -vh + 85, hint, 8);
  }
  if(!glgame->running && glship->show_help) {
    const char* unpause = glship->has_controller() ? "press start to resume" : "press p to resume";
    Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-80, unpause, 8);
  }

  // Touch: exit affordance — the bottom strip is a tap band
  // (GLGame::touch_tap), same placement as the lobby's return band. Shown
  // at GAME OVER, on the pause screen, and — online — when the LOCAL ship
  // is fully out while the peer plays on (all-over never fires there);
  // exit_band_showing() is that rule, shared with the badge rows' hoist.
  // Not under the touch roster, whose own BACK band takes the spot
  // (seat_roster draws it; two labels on one zone would lie about one).
  if(glgame->exit_band_showing() && !glgame->roster_open())
    glgame->exit_band().draw(
        Typer::cursored("EXIT TO MENU", true).c_str(),
        glgame->current_time);
  // The way into the touch roster: the online host's MANAGE PLAYERS band
  // on the pause screen, above the exit band (FOURPLAYER.md O3 touch
  // pass — a phone host had no way to see or remove the pilots).
  if(glgame->roster_touch_offer())
    glgame->roster_manage_band().draw("MANAGE PLAYERS");
}

void Overlay::draw_circle(float cx, float cy, float r, int segs, bool filled,
                          float cr, float cg, float cb, float ca) {
  static MeshBuilder mb;
  static Mesh mesh;
  mb.clear();
  mb.color(cr, cg, cb, ca);
  if (filled) {
    mb.begin(GL_TRIANGLE_FAN);
    mb.vertex(cx, cy);
  } else {
    mb.begin(GL_LINE_LOOP);
  }
  for (int i = 0; i <= segs; i++) {
    float angle = 2.0f * (float)M_PI * (float)i / (float)segs;
    mb.vertex(cx + r * cosf(angle), cy + r * sinf(angle));
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

// Weapon glyph for the shoot/mine circles: a small line icon naming the
// selected primary/secondary (Save::WeaponEntry::Kind), in the weapon
// family's own colour, echoing the pickup vocabulary (beam = violet star,
// lance = amber star, turret = teal ring-with-barrel, ...). r is the icon
// radius (a fraction of the button's), alpha follows the circle outline.
static void draw_weapon_glyph(uint8_t kind, float cx, float cy, float r,
                              float alpha) {
  static MeshBuilder mb;
  static Mesh mesh;
  mb.clear();
  mb.begin(GL_LINES);
  auto seg = [&](float x0, float y0, float x1, float y1) {
    mb.vertex(cx + x0 * r, cy + y0 * r);
    mb.vertex(cx + x1 * r, cy + y1 * r);
  };
  auto circle = [&](float rad, int segs) {
    for (int i = 0; i < segs; i++) {
      float a0 = 2.0f * (float)M_PI * i / segs;
      float a1 = 2.0f * (float)M_PI * (i + 1) / segs;
      seg(rad * cosf(a0), rad * sinf(a0), rad * cosf(a1), rad * sinf(a1));
    }
  };
  auto star4 = [&]() {  // the pickups' 4-point star
    seg(0.0f, 1.0f, 0.3f, 0.3f);   seg(0.3f, 0.3f, 1.0f, 0.0f);
    seg(1.0f, 0.0f, 0.3f, -0.3f);  seg(0.3f, -0.3f, 0.0f, -1.0f);
    seg(0.0f, -1.0f, -0.3f, -0.3f); seg(-0.3f, -0.3f, -1.0f, 0.0f);
    seg(-1.0f, 0.0f, -0.3f, 0.3f); seg(-0.3f, 0.3f, 0.0f, 1.0f);
  };
  switch ((Save::WeaponEntry::Kind)kind) {
    case Save::WeaponEntry::Kind::Default:
      // Gun: a crosshair.
      mb.color(1.0f, 1.0f, 1.0f, alpha);
      circle(0.55f, 12);
      seg(0.0f, 0.55f, 0.0f, 1.0f);   seg(0.0f, -0.55f, 0.0f, -1.0f);
      seg(0.55f, 0.0f, 1.0f, 0.0f);   seg(-0.55f, 0.0f, -1.0f, 0.0f);
      break;
    case Save::WeaponEntry::Kind::GodMode:
      // Sun: circle + rays.
      mb.color(1.0f, 0.85f, 0.3f, alpha);
      circle(0.45f, 10);
      for (int i = 0; i < 8; i++) {
        float a = 2.0f * (float)M_PI * i / 8;
        seg(0.6f * cosf(a), 0.6f * sinf(a), 1.0f * cosf(a), 1.0f * sinf(a));
      }
      break;
    case Save::WeaponEntry::Kind::Beam:
      mb.color(0.7f, 0.4f, 1.0f, alpha);  // violet star, like the pickup
      star4();
      break;
    case Save::WeaponEntry::Kind::Lance:
      mb.color(1.0f, 0.85f, 0.35f, alpha);  // amber star, like the pickup
      star4();
      break;
    case Save::WeaponEntry::Kind::Shock:
      // Lightning zigzag.
      mb.color(0.6f, 0.9f, 1.0f, alpha);
      seg(0.35f, 1.0f, -0.25f, 0.15f);
      seg(-0.25f, 0.15f, 0.25f, -0.05f);
      seg(0.25f, -0.05f, -0.35f, -1.0f);
      break;
    case Save::WeaponEntry::Kind::Mine:
      // Naval mine: circle + spikes.
      mb.color(1.0f, 1.0f, 1.0f, alpha);
      circle(0.5f, 10);
      for (int i = 0; i < 6; i++) {
        float a = 2.0f * (float)M_PI * i / 6;
        seg(0.5f * cosf(a), 0.5f * sinf(a), 0.95f * cosf(a), 0.95f * sinf(a));
      }
      break;
    case Save::WeaponEntry::Kind::GigaMine:
      // The mine, doubled up.
      mb.color(1.0f, 1.0f, 1.0f, alpha);
      circle(0.35f, 8);
      circle(0.6f, 10);
      for (int i = 0; i < 6; i++) {
        float a = 2.0f * (float)M_PI * i / 6 + 0.26f;
        seg(0.6f * cosf(a), 0.6f * sinf(a), 1.0f * cosf(a), 1.0f * sinf(a));
      }
      break;
    case Save::WeaponEntry::Kind::Missile:
      // Dart with fins, nose up.
      mb.color(0.95f, 0.95f, 0.95f, alpha);
      seg(0.0f, 1.0f, 0.3f, 0.3f);  seg(0.3f, 0.3f, 0.3f, -0.6f);
      seg(0.0f, 1.0f, -0.3f, 0.3f); seg(-0.3f, 0.3f, -0.3f, -0.6f);
      seg(0.3f, -0.6f, 0.7f, -1.0f); seg(-0.3f, -0.6f, -0.7f, -1.0f);
      seg(-0.3f, -0.6f, 0.3f, -0.6f);
      break;
    case Save::WeaponEntry::Kind::Shield:
      // Shield outline, point down.
      mb.color(0.4f, 0.9f, 1.0f, alpha);
      seg(-0.8f, 0.8f, 0.8f, 0.8f);
      seg(-0.8f, 0.8f, -0.8f, -0.1f); seg(0.8f, 0.8f, 0.8f, -0.1f);
      seg(-0.8f, -0.1f, 0.0f, -1.0f); seg(0.8f, -0.1f, 0.0f, -1.0f);
      break;
    case Save::WeaponEntry::Kind::Nova:
      // Starburst: rays only, alternating lengths.
      mb.color(1.0f, 0.6f, 0.2f, alpha);
      for (int i = 0; i < 8; i++) {
        float a = 2.0f * (float)M_PI * i / 8;
        float len = (i % 2 == 0) ? 1.0f : 0.55f;
        seg(0.15f * cosf(a), 0.15f * sinf(a), len * cosf(a), len * sinf(a));
      }
      break;
    case Save::WeaponEntry::Kind::Turret:
      // Ring with a barrel, like the pickup.
      mb.color(0.3f, 0.9f, 0.8f, alpha);
      circle(0.55f, 12);
      seg(0.0f, 0.55f, 0.0f, 1.1f);
      break;
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

void Overlay::touch_controls(const GLGame *glgame, const GLShip *glship) {
  // Touch platforms always; desktop only when the screenshot harness (or
  // the layout test hook) forces touch mode — see touch_osd_enabled().
  if (!touch_osd_enabled()) return;
  // Only the locally-controlled ship's viewport gets the OSD (on a net
  // client that is the LAST player, not the first). While spectating the
  // camera follows the peer, so this already fails; the extra guard also
  // hides the OSD during the "SPECTATING IN N" countdown, when the camera
  // is still on our own wreck but there is nothing left to control.
  if(glgame->local_player() != glship) return;
  if(glgame->spectate_arming() || glgame->is_spectating()) return;

  float pw = (float)Typer::window_width;
  float ph = (float)Typer::window_height;

  auto ox = [&](float px) { return 2.0f * px - pw; };
  auto oy = [&](float py) { return ph - 2.0f * py; };
  auto sr = [&](float r)  { return 2.0f * r; };

  const TouchControlsState &tc = g_touch_controls;

  // ---- Virtual joystick ----
  float jox = ox(tc.joy_hint_cx);
  float joy = oy(tc.joy_hint_cy);
  float jr  = sr(tc.joy_radius);

  if(tc.joy_active) {
    float bx = ox(tc.joy_cx);
    float by = oy(tc.joy_cy);
    draw_circle(bx, by, jr, 32, false, 0.5f, 0.65f, 1.0f, 0.75f);
    float nx_off =  tc.joy_nx * jr;
    float ny_off = -tc.joy_ny * jr;
    draw_circle(bx + nx_off, by + ny_off, jr * 0.38f, 32, true, 0.7f, 0.85f, 1.0f, 0.90f);
  } else {
    draw_circle(jox, joy, jr, 32, false, 0.4f, 0.55f, 1.0f, 0.55f);
    draw_circle(jox, joy, jr * 0.25f, 20, true, 0.4f, 0.55f, 1.0f, 0.40f);
  }

  // ---- Shoot button ----
  {
    float bx = ox(tc.shoot_cx);
    float by = oy(tc.shoot_cy);
    float br = sr(tc.shoot_radius);
    float alpha_fill    = tc.shoot_pressed ? 0.55f : 0.25f;
    float alpha_outline = tc.shoot_pressed ? 0.95f : 0.70f;
    draw_circle(bx, by, br, 28, true,  1.0f, 0.35f, 0.35f, alpha_fill);
    draw_circle(bx, by, br, 28, false, 1.0f, 0.35f, 0.35f, alpha_outline);
    glLineWidth(2.5f);
    draw_weapon_glyph(tc.primary_kind, bx, by, br * 0.45f, alpha_outline);
  }

  // ---- Mine button ----
  // Only while a secondary is equipped (GLGame::tick keeps the flag; the
  // entry points gate their hit test on the same flag, so the region and
  // the circle appear and vanish together).
  if (tc.mine_available) {
    float bx = ox(tc.mine_cx);
    float by = oy(tc.mine_cy);
    float br = sr(tc.mine_radius);
    float alpha_fill    = tc.mine_pressed ? 0.55f : 0.25f;
    float alpha_outline = tc.mine_pressed ? 0.95f : 0.70f;
    draw_circle(bx, by, br, 28, true,  0.35f, 0.6f, 1.0f, alpha_fill);
    draw_circle(bx, by, br, 28, false, 0.35f, 0.6f, 1.0f, alpha_outline);
    glLineWidth(2.5f);
    draw_weapon_glyph(tc.secondary_kind, bx, by, br * 0.45f, alpha_outline);
  }

  // ---- Boost button ----
  // Above and between the shoot/mine pair. Amber; dimmed while Ship's
  // cooldown runs (boost_ready, mirrored by GLGame::tick). The icon is a
  // double up-chevron — "more speed" in one glyph.
  {
    float bx = ox(tc.boost_cx);
    float by = oy(tc.boost_cy);
    float br = sr(tc.boost_radius);
    float dim = tc.boost_ready ? 1.0f : 0.35f;
    float alpha_fill    = (tc.boost_pressed ? 0.55f : 0.25f) * dim;
    float alpha_outline = (tc.boost_pressed ? 0.95f : 0.70f) * dim;
    draw_circle(bx, by, br, 28, true,  1.0f, 0.75f, 0.3f, alpha_fill);
    draw_circle(bx, by, br, 28, false, 1.0f, 0.75f, 0.3f, alpha_outline);

    static MeshBuilder mb;
    static Mesh mesh_icon;
    mb.clear();
    mb.begin(GL_TRIANGLES);
    mb.color(1.0f, 0.75f, 0.3f, alpha_outline);
    // Two stacked chevrons pointing up, each a thin bent band of two quads.
    float w2 = br * 0.42f, hh = br * 0.26f, th = br * 0.14f;
    for (int c = 0; c < 2; c++) {
      // Base offset chosen so the two-chevron ink block (spanning cy0-hh of
      // the lower chevron to cy0+th of the upper, 0.82*br tall) is centred
      // on the circle.
      float cy0 = by - br * 0.15f + c * br * 0.42f;
      // left stroke
      mb.vertex(bx - w2, cy0 - hh);      mb.vertex(bx, cy0);
      mb.vertex(bx - w2, cy0 - hh + th);
      mb.vertex(bx - w2, cy0 - hh + th); mb.vertex(bx, cy0);
      mb.vertex(bx, cy0 + th);
      // right stroke
      mb.vertex(bx + w2, cy0 - hh);      mb.vertex(bx, cy0);
      mb.vertex(bx + w2, cy0 - hh + th);
      mb.vertex(bx + w2, cy0 - hh + th); mb.vertex(bx, cy0);
      mb.vertex(bx, cy0 + th);
    }
    mb.end();
    mesh_icon.upload(mb, GL_DYNAMIC_DRAW);
    mesh_icon.draw();
  }

  // ---- Pause button ----
  {
    float bx = ox(tc.pause_cx);
    float by = oy(tc.pause_cy);
    float br = sr(tc.pause_radius);
    float alpha_fill    = tc.pause_active ? 0.35f : 0.08f;
    float alpha_outline = tc.pause_active ? 0.70f : 0.30f;
    draw_circle(bx, by, br, 32, true,  1.0f, 1.0f, 1.0f, alpha_fill);
    draw_circle(bx, by, br, 32, false, 1.0f, 1.0f, 1.0f, alpha_outline);

    static MeshBuilder mb;
    static Mesh mesh_icon;
    mb.clear();
    if (glgame->running) {
      // Two vertical bars (pause icon) — GL_QUADS replaced with GL_TRIANGLES
      float bw  = br * 0.15f;
      float bh  = br * 0.38f;
      float sep = br * 0.20f;
      mb.begin(GL_TRIANGLES);
      mb.color(1.0f, 1.0f, 1.0f, alpha_outline);
      // Left bar
      mb.vertex(bx - sep - bw*2, by - bh);
      mb.vertex(bx - sep,        by - bh);
      mb.vertex(bx - sep,        by + bh);
      mb.vertex(bx - sep - bw*2, by - bh);
      mb.vertex(bx - sep,        by + bh);
      mb.vertex(bx - sep - bw*2, by + bh);
      // Right bar
      mb.vertex(bx + sep,        by - bh);
      mb.vertex(bx + sep + bw*2, by - bh);
      mb.vertex(bx + sep + bw*2, by + bh);
      mb.vertex(bx + sep,        by - bh);
      mb.vertex(bx + sep + bw*2, by + bh);
      mb.vertex(bx + sep,        by + bh);
      mb.end();
    } else {
      // Right-pointing triangle (play/resume icon)
      float th = br * 0.45f;
      float tx = bx - br * 0.05f;
      mb.begin(GL_TRIANGLES);
      mb.color(1.0f, 1.0f, 1.0f, alpha_outline);
      mb.vertex(tx - th * 0.6f, by - th);
      mb.vertex(tx + th,        by);
      mb.vertex(tx - th * 0.6f, by + th);
      mb.end();
    }
    mesh_icon.upload(mb, GL_DYNAMIC_DRAW);
    mesh_icon.draw();
  }
}

void Overlay::debug_info(const GLGame *glgame, const GLShip *glship) {
  // Only draw once — skip for the second player's viewport. The primary
  // viewport is player 1 offline, but ONLINE the single viewport belongs
  // to the LOCAL player — on the client that is the BACK of the list
  // (the front is the remote host), and comparing against the front
  // blanked the whole debug stack on every client.
  const GLShip *primary = glgame->net_active() ? glgame->local_player()
                                               : glgame->players->front();
  if (glship != primary) return;

  // Rolling FPS: count frames over ~500 ms windows so the reading reflects
  // current performance rather than the lifetime average.
  static Uint32 fps_window_start = 0;
  static int    fps_window_frames = 0;
  static int    fps_display = 0;
  Uint32 now = SDL_GetTicks();
  if (fps_window_start == 0) fps_window_start = now;
  fps_window_frames++;
  Uint32 elapsed = now - fps_window_start;
  if (elapsed >= 500) {
    fps_display = (int)(fps_window_frames * 1000u / elapsed);
    fps_window_frames = 0;
    fps_window_start  = now;
  }

  float vw = Typer::scaled_window_width / glgame->num_x_viewports();
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  float x  = -vw + CORNER_INSET;
  float sz = 7;
  float dy = sz * 2.5f + 4;
  // Anchor the stack's TOP at a fixed height on the left edge: below the
  // weapons HUD (top ~0.6*vh) and safely above the minimap, whose top
  // reaches ~-vh/2 — the old vertically-centred stack sank into the
  // minimap corner on some aspects once it grew past two lines.
  float y = vh * 0.35f;

  char fps_buf[32];
  snprintf(fps_buf, sizeof(fps_buf), "fps: %d", fps_display);

  const char *gm_str = "game mode: off";
  switch (game_mode_status()) {
    case GameModeStatus::On:    gm_str = "game mode: on";    break;
    case GameModeStatus::Ready: gm_str = "game mode: ready"; break;
    case GameModeStatus::Off:   gm_str = "game mode: off";   break;
  }
  // Online: the selected ICE path ("net: host/host" direct LAN,
  // "srflx/..." NAT-punched, "relay/..." through TURN) — the fast way to
  // see whether a session is burning relay bandwidth — plus the smoothed
  // MSG_PING round trip once the first PONG lands.
  char net_buf[80] = "";
  if (glgame->net_active() && glgame->net_session() &&
      glgame->net_session()->transport()) {
    std::string ci = glgame->net_session()->transport()->connection_info();
    if (!ci.empty()) {
      if (glgame->net_rtt_ms() >= 0.0f)
        snprintf(net_buf, sizeof(net_buf), "net: %s %dms", ci.c_str(),
                 (int)(glgame->net_rtt_ms() + 0.5f));
      else
        snprintf(net_buf, sizeof(net_buf), "net: %s", ci.c_str());
    }
  }

  Typer::draw(x, y,      gm_str, sz);
  Typer::draw(x, y - dy, fps_buf, sz);
  float next_y = y - dy * 2;
#ifdef STEAM_BUILD
  std::string branch = get_steam_branch();
  char branch_buf[64];
  snprintf(branch_buf, sizeof(branch_buf), "branch: %s",
           branch.empty() ? "default" : branch.c_str());
  Typer::draw(x, next_y, branch_buf, sz);
  next_y -= dy;
#endif
  if (net_buf[0]) Typer::draw(x, next_y, net_buf, sz);
}
