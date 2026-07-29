#include "tap_band.h"

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
