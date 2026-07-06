#ifndef OVERLAY_H
#define OVERLAY_H

class GLShip;
class GLGame;

class Overlay {
public:
  static const float CORNER_INSET;
  // Fraction of each viewport axis that UI may occupy. TVs can crop the
  // outer ~5% per edge (overscan), and Xbox certification requires critical
  // UI inside the title-safe region, so GDK builds shrink the HUD/menu
  // projection to the central 90%. 1.0 everywhere else (no change).
  static const float SAFE_AREA_SCALE;
  static void draw(const GLGame * glgame, const GLShip *glship);
  // Full-screen text layered over the online game view (one full-screen
  // pass, not per-viewport): the generation banner and CONNECTION LOST card.
  static void net_overlays(const GLGame *glgame);
  // Touch OSD (joystick, fire/mine, pause) — public so the intro screen can
  // draw it too; no-op off Android/iOS. Needs a full-window ortho of
  // ±window extents to be current.
  static void touch_controls(const GLGame *glgame, const GLShip *glship);

private:
  static void score(const GLGame *glgame, const GLShip *glship);
  static void level_cleared(const GLGame *glgame, const GLShip *glship);
  static void lives(const GLGame *glgame, const GLShip *glship);
  static void weapons(const GLGame *glgame, const GLShip *glship);
  static void temperature(const GLGame *glgame, const GLShip *glship);
  static void respawn_timer(const GLGame *glgame, const GLShip *glship);
  static void keymap(const GLGame *glgame, const GLShip *glship);
  static void title_text(const GLGame *glgame, const GLShip *glship);
  static void paused(const GLGame *glgame, const GLShip *glship);
  static void level(const GLGame *glgame, const GLShip *glship);
  static void god_mode(const GLGame *glgame, const GLShip *glship);
  static void edge_indicators(const GLGame *glgame, const GLShip *glship);
  static void debug_info(const GLGame *glgame, const GLShip *glship);
  static void draw_circle(float cx, float cy, float r, int segs, bool filled,
                          float cr, float cg, float cb, float ca);
};

#endif
