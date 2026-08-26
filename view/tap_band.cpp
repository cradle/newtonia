#include "tap_band.h"

#include "../gl_compat.h"  // is_touch_mode
#include "../typer.h"

const TapBand TapBand::return_to_menu(0.5f, -420, 13, 50.0f, false, true);

// Replay controls: side-by-side thirds at one height, clear of the exit
// band below (its reach tops out around -370). Landscape geometry —
// bottom_lift() re-anchors the whole stack for portrait.
const TapBand TapBand::replay_slower(0.18f, -300, 14, 30.0f, false, false,
                                     0.00f, 0.35f);
const TapBand TapBand::replay_pause(0.50f, -300, 14, 30.0f, false, false,
                                    0.35f, 0.65f);
const TapBand TapBand::replay_faster(0.82f, -300, 14, 30.0f, false, false,
                                     0.65f, 1.00f);

float TapBand::bottom_lift() {
  // Negative in portrait (the stretched half-height), pushing anchors back
  // down to the bottom edge they were tuned against; exactly 0 in
  // landscape, where scaled == original.
  return (float)Typer::original_window_height - Typer::scaled_window_height;
}

TapBand TapBand::lifted(float dy) const {
  return TapBand(nx, y + dy, size, pad, to_top, to_bottom, nx_min, nx_max);
}

// Name lines at 230 / 80 / -70 (pitch 150, the touch board list's), the
// action zones 56 under each: block 2's zone bottoms out at -178, clear
// of the anon band the in-game roster hangs at -260 and of every bottom
// band (the lobby's exit zone starts at -370).
float TapBand::roster_row_y(int block) { return 230.0f - 150.0f * block; }

TapBand TapBand::roster_action(int block, bool ban_half, bool split) {
  float y = roster_row_y(block) - 56.0f;
  if (!split) return TapBand(0.5f, y, 14, 24.0f, false, false, 0.20f, 0.80f);
  return ban_half ? TapBand(0.70f, y, 14, 24.0f, false, false, 0.50f, 0.95f)
                  : TapBand(0.30f, y, 14, 24.0f, false, false, 0.05f, 0.50f);
}

const TapBand TapBand::roster_anon(0.5f, -260, 13, 16.0f);

TapBand TapBand::for_pointer() const {
  if (is_touch_mode()) return *this;
  // A mouse lands where it is pointed: no finger pad, and no edge run —
  // an edge band exists so a thumb near the bezel still counts, which is
  // not a thing a cursor does.
  return TapBand(nx, y, size, 6.0f, false, false, nx_min, nx_max);
}

void TapBand::draw(const char *text, int time) const {
  float vx = (2.0f * nx - 1.0f) * Typer::scaled_window_width;
  Typer::draw_centered(vx, y, text, (float)size, time);
}

bool TapBand::contains(float tx, float ty) const {
  if (tx < nx_min || tx > nx_max) return false;
  float vy = (1.0f - 2.0f * ty) * Typer::scaled_window_height;
  float mid = y - size;  // centre of the glyph box (glyphs reach y - 2*size)
  float half = size + pad;
  if (!to_top && vy > mid + half) return false;
  if (!to_bottom && vy < mid - half) return false;
  return true;
}
