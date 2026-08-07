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
static const int PAUSE_ROW_SZ = 13, PAUSE_ROW_Y0 = -42, PAUSE_ROW_Y1 = -80;

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
        Typer::cursored("RETURN TO MENU", true).c_str(), now);
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
      MenuSelect::draw_row(-100, "RETURN TO MENU", 16, true);
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

  if (!all_game_over && glgame->net_banner_ms_ <= 0 &&
      !glgame->net_connection_lost_)
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

  if (all_game_over) {
    // The offline hints live in the single-player title_text branch, so
    // the online game (always 2 players) had NO game-over text on either
    // screen. One shared card for both roles; the 3 s guard in the input
    // handlers still stops a mid-fight trigger from skipping it.
    // When a leaderboard flow is live, board_prompt() owns the ENTIRE
    // game-over card (heading + prompt/result + its own RETURN TO MENU
    // row), in every mode — so this online card cedes it completely to
    // avoid drawing GAME OVER (and the row) twice.
    if (glgame->board_phase_ != GLGame::BoardOff) return;
    Typer::draw_centered(0, 60, "GAME OVER", 34);
    // Every screen whose only move is "leave" says so the same way: the
    // shared RETURN TO MENU row, answered by confirm or back. Touch draws
    // no cursor and already has the tap band under this card (title_text
    // shows it whenever the game is over), so the row would just repeat it.
    if (!is_touch_mode())
      MenuSelect::draw_row(-80, "RETURN TO MENU", 16, true);
    return;
  }

  if (glgame->net_connection_lost_ && glgame->net_mode_ == GLGame::NetHost &&
      (glgame->net_signal_ || glgame->net_lan_door_open())) {
    // Rejoinable loss: the game continues — a quiet notice, not a card.
    // A "P2 DISCONNECTED" header over the room code (steady, no blink): the
    // host may be reading the code out to the other player, and it explains
    // why the code is back on screen. Named when the peer's identity is
    // known ("GLENN DISCONNECTED"); a legacy peer keeps the plain text.
    // A LAN-door session has no code — the reopened beacon is the way
    // back, so say that instead.
    std::string who =
        net_identity_name_or(glgame->net_peer_identity_,
                             glgame->net_peer_fallback().c_str(),
                             glgame->net_id_ctx());
    Typer::draw_centered(0, vh * 0.80f, (who + " DISCONNECTED").c_str(), 20);
    if (glgame->net_signal_) {
      std::string room = "ROOM " + glgame->net_room_code_;
      Typer::draw_centered(0, vh * 0.67f, room.c_str(), 18);
    }
    if (glgame->net_lan_door_open())
      Typer::draw_centered(0, vh * (glgame->net_signal_ ? 0.57f : 0.67f),
                           "VISIBLE ON THIS NETWORK", 14);
  } else if (glgame->net_connection_lost_ &&
             glgame->net_mode_ == GLGame::NetClient &&
             (!glgame->net_room_code_.empty() ||
              !glgame->net_lan_host_name_.empty())) {
    Typer::draw_centered(0, 60, "CONNECTION LOST", 34);
    if ((now / 700) % 2 == 0)
      Typer::draw_centered(0, -80, "REJOINING", 16);
  } else if (glgame->net_connection_lost_) {
    if (glgame->net_peer_bye_) {
      // Named like DISCONNECTED/RECONNECTED but near the middle, at the
      // banner spot ("GLENN LEFT THE GAME"; the host is player 1, so a
      // nameless/legacy host reads "PLAYER 1 LEFT THE GAME"). Clear of the
      // pause overlay's "Paused" at y=30 — both show when the host
      // leaves a paused game.
      std::string who =
          net_identity_name_or(glgame->net_peer_identity_,
                               glgame->net_peer_fallback().c_str(),
                               glgame->net_id_ctx());
      Typer::draw_centered(0, vh * 0.55f, (who + " LEFT THE GAME").c_str(),
                           22);
    } else {
      // y=160, not 60: clear of the pause overlay's "Paused" at y=30.
      Typer::draw_centered(0, 160, "CONNECTION LOST", 34);
    }
    // y=-130: clear of where the pause overlay's menu rows sit (RESUME at
    // -42, RETURN TO MENU at -80, glyphs reaching ~-106). Those rows are
    // never drawn beside this card any more — the input handlers answer
    // only THIS row while the link is down, so pause_menu_active() refuses
    // and a loss landing on a paused game shows just the "Paused" heading
    // (the host pausing and then leaving used to give the client two dead
    // rows above this live one). The position keeps the historical gap so
    // nothing shifts.
    // A menu row, not an any-key prompt: confirm or back leaves and
    // nothing else does, so a stray keypress can't end the session.
    // Steady, not flashing — a cursor row is a thing you act on, not an
    // alert. Touch draws no cursor and takes the tap band instead, which
    // title_text now shows on a lost link like it does at game over.
    if (!is_touch_mode())
      MenuSelect::draw_row(-130, "RETURN TO MENU", 16, true);
  } else if (glgame->net_banner_ms_ > 0) {
    // A header banner ("<NAME> RECONNECTED") takes the DISCONNECTED
    // header's exact position and size, so the notice swaps in place
    // rather than jumping down the screen when the peer returns.
    if (glgame->net_banner_header_)
      Typer::draw_centered(0, vh * 0.80f, glgame->net_banner_text_.c_str(), 20);
    else
      Typer::draw_centered(0, vh * 0.55f, glgame->net_banner_text_.c_str(), 22);
  }
}

void Overlay::draw(const GLGame *glgame, const GLShip *glship) {
  // Replay playback: keep the run's own HUD (score/lives/weapons/level —
  // part of the story) but drop every overlay that invites input the
  // replay won't take — join/help hints, the keymap card, the touch OSD
  // (replay_hud carries the touch exit hint; R3 owns richer touch UX).
  bool replaying = glgame->net_mode_ == GLGame::NetReplay;
  if (!replaying) title_text(glgame, glship);
  level(glgame, glship);
  god_mode(glgame, glship);
  score(glgame, glship);
  if (!replaying) keymap(glgame, glship);
  level_cleared(glgame, glship);
  lives(glgame, glship);
  weapons(glgame, glship);
  temperature(glgame, glship);
  respawn_timer(glgame, glship);
  spectate(glgame, glship);
  remote_badge(glgame, glship);
  paused(glgame, glship);
  if (!replaying) touch_controls(glgame, glship);
  edge_indicators(glgame, glship);
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

void Overlay::paused(const GLGame *glgame, const GLShip *glship) {
  // Once the game is over, the game-over messaging (the shared GAME OVER
  // card online / in replays, the per-ship indicator offline) owns the
  // centre of the screen — never stack "Paused" under it. toggle_pause
  // refuses new pauses on a finished game; this covers a game that ENDS
  // while already paused (e.g. the host leaving a paused game is terminal
  // for a spectating client).
  if (glgame->all_players_out()) return;
  if(!glgame->running && !glship->show_help) {
    Typer::draw_centered(0, 30, "Paused", 25);
    if(is_touch_mode()) {
      // Touch has no cursor on any screen: the pause button resumes and the
      // RETURN TO MENU band below leaves.
      Typer::draw_centered(0, -40, "press play to resume", 8);
      return;
    }
    // Selectable rows carrying the shared menu cursor, sized so the stack
    // (size 13 rows at -42 and -80) bottoms out around -106, clear of the
    // disconnect card's row at y=-130. The two no longer show at once —
    // pause_menu_active() refuses while that card owns input — but the
    // positions stay put so neither screen shifts.
    //
    // Ask the game whether the menu is LIVE rather than re-deriving it. The
    // conditions had already drifted apart: this function tests the help
    // card on the viewport's own ship, while pause_menu_active() refuses
    // when ANY player has help open — so in split-screen, with P2's help
    // card up and the game paused, P1's viewport drew a highlighted RESUME
    // over a RETURN TO MENU that answered nothing. Same shape as the three
    // replay ones. One predicate, both sides.
    if (!glgame->pause_menu_active()) return;
    MenuSelect::draw_row(PAUSE_ROW_Y0, "RESUME", PAUSE_ROW_SZ,
                         glgame->pause_selection_ == GLGame::PAUSE_RESUME);
    MenuSelect::draw_row(PAUSE_ROW_Y1, "RETURN TO MENU", PAUSE_ROW_SZ,
                         glgame->pause_selection_ == GLGame::PAUSE_EXIT);
  }
}


void Overlay::level(const GLGame *glgame, const GLShip *glship) {
  char buf[20];
  snprintf(buf, sizeof(buf), "LEVEL %d", glgame->generation + 1);
  Typer::draw_centered(0, top_hud_y(glgame), buf, 12);
}

void Overlay::god_mode(const GLGame *glgame, const GLShip *glship) {
  int remaining = glship->ship->god_mode_time_remaining();
  if(remaining <= 0) return;
  float base_y = top_hud_y(glgame);
  Typer::draw_centered(0, base_y - 62, "God mode", 10);
  Typer::draw_centered(0, base_y - 100, remaining / 1000, 10);
}

void Overlay::score(const GLGame *glgame, const GLShip *glship) {
  float vw = Typer::scaled_window_width / glgame->num_x_viewports();
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  float top_y = top_hud_y(glgame);
  float drop = (vh - 20 - CORNER_INSET) - top_y;  // cutout shift, 0 without one
  Typer::draw(vw - 40 - CORNER_INSET, top_y, glship->ship->score, 20);
  if(glship->ship->multiplier() > 1) {
    Typer::draw(vw - 35 - CORNER_INSET, vh - 92 - CORNER_INSET - drop, "x", 15);
    Typer::draw(vw - 65 - CORNER_INSET, vh - 80 - CORNER_INSET - drop, glship->ship->multiplier(), 20);
  }
}

void Overlay::level_cleared(const GLGame *glgame, const GLShip *glship) {
  if(glgame->running && glgame->level_cleared && (glship->ship->is_alive() || glship->ship->lives > 0)) {
    Typer::draw_centered(0, 150, "CLEARED", 50);
    Typer::draw_centered(0, -60, (glgame->time_until_next_generation / 1000)+1, 20);
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

// Spectator flow (netplay co-op): a "SPECTATING IN N" countdown on the local
// wreck, then "SPECTATING" at the bottom once the camera has handed off to the
// peer. Both phases are driven by GLGame::spectate_death_time_.
void Overlay::remote_badge(const GLGame *glgame, const GLShip *glship) {
  (void)glship;
  if (!glgame->net_active()) return;
  // The badge names the REMOTE player: the client looks at the host
  // (player 1), the host at the client (player 2).
  std::string badge = net_identity_badge_or(
      glgame->net_peer_identity_, glgame->net_peer_fallback().c_str(),
      glgame->net_id_ctx());
  if (badge.empty()) return;  // legacy peer: no badge, no placeholder
  // Bottom row like the SPECTATING hint, clear of the touch RETURN TO MENU
  // band and the title-safe margin; hoisted when the camera is on the peer
  // (that's exactly when the tag matters most). The spectating hoist must
  // clear the WHOLE exit band, not just the SPECTATING text: the band's
  // glyph box tops out ~-370 (anchor -420, size 13, pad 50, to_bottom
  // below), and the old +175 spot printed the badge exactly on the band's
  // RETURN TO MENU text (field: iPhone, 2026-07-24). +255 sits above the
  // band's reach with air to spare.
  float vhb = -Typer::scaled_window_height / glgame->num_y_viewports();
  float y = glgame->is_spectating() ? vhb + 255.0f : vhb + 130.0f;
  // A worker-attested peer earns the verified tick; a LAN/manual peer's badge
  // is a claim and draws bare (net_identity_verified owns that rule).
  Typer::draw_centered_verified(
      0, y, badge.c_str(), 11,
      net_identity_verified(glgame->net_peer_identity_, glgame->net_id_ctx()));
}

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
    // Bottom of the viewport, clear of the touch RETURN TO MENU band.
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
  // own RETURN TO MENU row — everything on ONE spaced layout, no collision
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
  // Terminal phases: a result line, then the RETURN TO MENU row (this
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
  // Touch already has the RETURN TO MENU tap band under the card (title_text
  // draws it whenever the game is over); desktop/controller get the row.
  if (!touch)
    MenuSelect::draw_row(-60, "RETURN TO MENU", 16, true);
}

void Overlay::keymap(const GLGame *glgame, const GLShip *glship) {
  if(glship->show_help) {
    glship->draw_keymap();
  }
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

void Overlay::title_text(const GLGame *glgame, const GLShip *glship) {
  Ship* p1 = glgame->players->front()->ship;
  if(glgame->players->size() < 2) {
    // -40 (not -10): a real margin inside the title-safe edge, matching the
    // bottom-row hints (Xbox compliance) — pulled down further by the
    // cutout inset so this row stays aligned with the LEVEL/score/weapons
    // row below the camera notch.
    float top_y = Typer::scaled_window_height - 40 - safe_inset_top_v();
    if(p1->is_alive() || p1->lives > 0) {
      // The join invitation blinks — it is an offer, not the only move here.
      if((glgame->current_time/1400) % 2 && !is_touch_mode()) {
        if(glgame->has_free_controller())
          Typer::draw_centered(Typer::scaled_window_width/2, top_y, "player 2 press start to join", 8);
#ifndef _GAMING_XBOX
        // Keyboard join hint — on Xbox the only join path is a second controller.
        else if(!is_steam_gamemode())
          Typer::draw_centered(Typer::scaled_window_width/2, top_y, "player 2 press enter to join", 8);
#endif
      }
    } else if(!is_touch_mode()) {
      // Game over: the same row, at the same size, that the online GAME
      // OVER card and the disconnect card draw — one exit affordance across
      // every end-state instead of a hint naming a different key per
      // screen, and steady like those, because it IS the move here.
      // BELOW the ending like the online card (which stacks GAME OVER at
      // 60 over the row at -80): draw_respawn_timer's 20x viewport puts
      // "GameOver" at y 80..0 and the score at -20..-60 in this space, so
      // -100 clears both. Suppressed while a leaderboard flow owns the
      // card (board_prompt draws its own heading + RETURN TO MENU row).
      if (glgame->board_phase_ == GLGame::BoardOff)
        MenuSelect::draw_row(-100, "RETURN TO MENU", 16, true);
    }
    if(!glship->last_input_was_controller && !is_touch_mode()) {
      char hint[48];
      if(glship->show_help) {
        key_hint(glship->help_key.primary(), hint, sizeof(hint), "hide");
        Typer::draw_centered(-1*Typer::scaled_window_width/2, top_y, hint, 8);
      } else if ((glgame->current_time)/12000 % 2) {
        key_hint(glship->help_key.primary(), hint, sizeof(hint), "show");
        Typer::draw_centered(-1*Typer::scaled_window_width/2, top_y, hint, 8);
      }
    }
  } else {
    float vhb = -Typer::scaled_window_height/glgame->num_y_viewports();
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
    // Label the key this ship actually has bound — online the client's
    // local ship is player 2 in the list but plays with player-1 keys.
    if(!glship->last_input_was_controller && !is_touch_mode()) {
      char hint[48];
      if(glship->show_help) {
        key_hint(glship->help_key.primary(), hint, sizeof(hint), "hide");
        Typer::draw_centered(0, vhb+85, hint, 8);
      } else if ((glgame->current_time)/12000 % 2) {
        key_hint(glship->help_key.primary(), hint, sizeof(hint), "show");
        Typer::draw_centered(0, vhb+85, hint, 8);
      }
    }
  }
  if(!glgame->running && glship->show_help) {
    const char* unpause = glship->has_controller() ? "press start to resume" : "press p to resume";
    Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-80, unpause, 8);
  }

  // Touch: exit affordance — the bottom strip is a tap band
  // (GLGame::touch_tap), same placement as the lobby's return band. Shown
  // at GAME OVER, on the pause screen, and — online — when the LOCAL ship
  // is fully out while the peer plays on (all-over never fires there).
  if(is_touch_mode()) {
    bool all_over = !glgame->players->empty();
    for(auto* gs : *glgame->players) {
      if(gs->ship->is_alive() || gs->ship->lives > 0) { all_over = false; break; }
    }
    const GLShip* local = glgame->local_player();
    bool local_over = glgame->net_active() && local &&
                      !local->ship->is_alive() && local->ship->lives <= 0;
    if(all_over || !glgame->running || local_over ||
       glgame->net_connection_lost_)
      glgame->exit_band().draw(
          Typer::cursored("RETURN TO MENU", true).c_str(),
          glgame->current_time);
  }
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
  }

  // ---- Mine button ----
  {
    float bx = ox(tc.mine_cx);
    float by = oy(tc.mine_cy);
    float br = sr(tc.mine_radius);
    float alpha_fill    = tc.mine_pressed ? 0.55f : 0.25f;
    float alpha_outline = tc.mine_pressed ? 0.95f : 0.70f;
    draw_circle(bx, by, br, 28, true,  0.35f, 0.6f, 1.0f, alpha_fill);
    draw_circle(bx, by, br, 28, false, 0.35f, 0.6f, 1.0f, alpha_outline);
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
  if (glgame->net_active() && glgame->net_session_ &&
      glgame->net_session_->transport()) {
    std::string ci = glgame->net_session_->transport()->connection_info();
    if (!ci.empty()) {
      if (glgame->net_rtt_ms_ >= 0.0f)
        snprintf(net_buf, sizeof(net_buf), "net: %s %dms", ci.c_str(),
                 (int)(glgame->net_rtt_ms_ + 0.5f));
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
