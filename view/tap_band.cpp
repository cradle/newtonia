#include "tap_band.h"

#include "../typer.h"

const TapBand TapBand::return_to_menu(0.5f, -420, 13, 50.0f, false, true);

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
