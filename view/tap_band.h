#ifndef TAP_BAND_H
#define TAP_BAND_H

// TapBand — a tappable text band. ONE definition yields both the drawn
// label and the touch hit-test, so text and tap zone can never drift
// apart (three playtest bugs came from moving one without the other).
//
// Vertical geometry rides the Typer anchor: `y` is the draw_centered
// anchor in Typer virtual coordinates (+y up; glyphs extend ~2*size
// BELOW the anchor), and the band covers the glyph box plus `pad` above
// and below — or all the way to a screen edge with to_top/to_bottom
// (an edge band leaves no dead sliver past its text). Horizontal
// geometry is normalized: `nx` anchors the drawn text (0.5 = centre)
// and [nx_min, nx_max] bounds the tappable strip (default full width).
// contains() takes the normalized (0..1, y-down) coords touch_tap gets.
struct TapBand {
  float nx, y;
  int size;
  float pad;
  bool to_top, to_bottom;
  float nx_min, nx_max;

  constexpr TapBand(float nx_anchor, float y_anchor, int glyph_size,
                    float finger_pad, bool extend_top = false,
                    bool extend_bottom = false, float min_nx = 0.0f,
                    float max_nx = 1.0f)
      : nx(nx_anchor), y(y_anchor), size(glyph_size), pad(finger_pad),
        to_top(extend_top), to_bottom(extend_bottom), nx_min(min_nx),
        nx_max(max_nx) {}

  // Draws the label on the band's anchor; pass the current time for
  // Typer's flashing variant (0 = steady).
  void draw(const char *text, int time = 0) const;
  bool contains(float tx, float ty) const;

  // The universal bottom exit strip: drawn by the in-game overlay and
  // the lobby, hit-tested by GLGame::touch_tap and NetLobby::touch_tap.
  static const TapBand return_to_menu;

  // Replay playback controls (REPLAY.md R3): a three-way strip stacked
  // above the exit band — SLOWER | PAUSE/RESUME | FASTER. Drawn by
  // Overlay::replay_hud, hit-tested by GLGame::touch_tap in NetReplay.
  // Their anchors below are LANDSCAPE geometry: apply bottom_lift() at
  // both sites (see below).
  static const TapBand replay_slower, replay_pause, replay_faster;

  // Turns a landscape-tuned anchor into a bottom-anchored one. The fixed
  // anchors above are measured against a 600 virtual half-height, which
  // IS the landscape bottom strip; portrait stretches the half-height to
  // 600/aspect (~1300+ on a tall phone), stranding those anchors near
  // mid-screen where replay chrome sits on top of the action instead of
  // under it. This is the same portrait-stretch bug GLGame::exit_band()
  // was re-anchored for. The lift is 0 whenever the virtual height is
  // unstretched, so landscape keeps its tuned stack byte-for-byte.
  //
  // Both the draw and the hit-test must apply the SAME lift or the label
  // and its tap zone drift apart — the whole invariant this struct
  // exists to hold. Overlay::replay_hud and GLGame::touch_tap are the
  // two sites.
  static float bottom_lift();
  TapBand lifted(float dy) const;
};

#endif
