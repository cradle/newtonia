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

  // The same band sized for the pointer actually in use. A finger needs the
  // pad and the run to the screen edge; a mouse cursor needs neither, and
  // paying for a finger with a mouse is what made the bottom ~19% of the
  // lobby window a menu exit — an idle click low on the screen left the
  // room (field, 2026-08-13). Off touch this tightens to the glyph box
  // plus a hairline; on touch it is the band unchanged. Hit-test only:
  // the drawn text never moves, so the tap zone stays a subset of the ink
  // it belongs to.
  TapBand for_pointer() const;

  // ---- Touch roster blocks (FOURPLAYER.md O3, touch pass) ----
  // The lobby's manage view and the in-game host roster draw the SAME
  // touch layout — a name line per remote pilot with finger-sized action
  // zones under it — so the geometry lives here, once, feeding both
  // surfaces' draw AND hit-test (the TapBand rule, across two screens).
  // Blocks 0..2 cover seats 2..4; fixed top-anchored slots, so portrait's
  // stretched half-height spreads them mid-screen like every other lobby
  // anchor.
  static const int ROSTER_BLOCKS = 3;
  static float roster_row_y(int block);  // the name line's Typer anchor
  // The action zone(s) under a name line. split = the row offers BAN too
  // (a worker-attested name): KICK takes the left half, BAN the right.
  // Unsplit rows centre the lone KICK. ban_half picks which of a split
  // row's zones.
  static TapBand roster_action(int block, bool ban_half, bool split);
  // The in-game roster's ALLOW ANONYMOUS PLAYERS band, under the blocks
  // (the lobby's main screen carries its own copy of the policy band).
  static const TapBand roster_anon;
};

// TouchZone — a rectangular in-game tap zone in the normalized (0..1,
// y-down) coordinates touch_tap receives. ONE definition feeds the draw
// (Overlay::touch_zoom) and the hit-test (GLGame::touch_tap): the TapBand
// rule for a zone that carries a glyph instead of a label.
struct TouchZone {
  float nx0, ny0, nx1, ny1;
  constexpr TouchZone(float x0, float y0, float x1, float y1)
      : nx0(x0), ny0(y0), nx1(x1), ny1(y1) {}
  bool contains(float nx, float ny) const {
    return nx >= nx0 && nx < nx1 && ny >= ny0 && ny < ny1;
  }
  float cx() const { return 0.5f * (nx0 + nx1); }
  float cy() const { return 0.5f * (ny0 + ny1); }
  constexpr TouchZone mirrored_x() const {
    return TouchZone(1.0f - nx1, ny0, 1.0f - nx0, ny1);
  }

  // ---- In-game touch zoom zones ----
  // "+" (closer) stacked above "-" (wider) on the right edge, each tap
  // stepping the pilot's ZOOM pref one Options step (GLShip::step_zoom).
  // The column sits in the hole every touch layout leaves: below the
  // native pause circle's hit zone (bottom ~0.39h landscape, ~0.15h
  // portrait) and the native right-half '\r' strip (y < 0.4); above the
  // mine button's hit circle (top ~0.63h landscape, ~0.76h portrait);
  // right of the boost circle (x <= ~0.87w) and the centre pause zone
  // (x <= 0.62). On web the canvas fallback above the buttons (y < 0.65)
  // is a dead zone and the HTML pause button ends at y 0.25. So a
  // finger-down here synthesizes nothing on any platform, and the release
  // reaches touch_tap — no entry-point changes, and the web build gets
  // the zones without an HTML counterpart.
  static const TouchZone zoom_in, zoom_out;
  // The zones as PLACED for the current layout: HANDEDNESS LEFT
  // (Preferences::touch_handedness, both input methods) mirrors the
  // column to the LEFT edge — the busy thumb's side. Every consumer —
  // Overlay::touch_zoom's draw, GLGame::touch_tap's hit test, the
  // one-hand gesture layer's carve-out, and web/main.ts's inZoomZone
  // twin — must take these, never the raw statics, or the drawn glyphs
  // and the hit tests drift apart (the TapBand rule).
  static TouchZone zoom_in_placed();
  static TouchZone zoom_out_placed();
};

#endif
