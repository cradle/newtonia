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
  // Display-cutout safe-area insets (camera notch / punch-hole), physical
  // pixels from each screen edge. Reported by the platform entry point
  // (Android: NewtoniaActivity's DisplayCutout via android_main, re-read on
  // rotation); 0 everywhere else. The top-anchored HUD row shifts down by
  // the top inset so LEVEL/score/weapons clear the camera in portrait.
  static void set_safe_insets(float top, float bottom, float left, float right);
  static float safe_inset_top();
  static void draw(const GLGame * glgame, const GLShip *glship);
  // Full-screen text layered over the online game view (one full-screen
  // pass, not per-viewport): the generation banner and CONNECTION LOST card.
  static void net_overlays(const GLGame *glgame);
  // The leaderboard game-over prompt / upload-progress / result block
  // (LEADERBOARD.md). Drawn as its OWN full-window overlay from
  // GLGame::draw() in EVERY mode — the offline solo game is the primary
  // leaderboard case, and its game-over card is not net_overlays. No-op
  // unless board_phase_ is active. When it owns the lower half (a live
  // prompt or an upload in flight, GLGame::board_prompt_active()) the
  // GAME OVER cards suppress their own RETURN TO MENU row.
  static void board_prompt(const GLGame *glgame);
  // Replay playback chrome (REPLAY.md R2, one full-screen pass): REPLAY
  // watermark (+ speed when not 1x), elapsed/total timeline, the flashing
  // exit hint once the recording ends short of a game over.
  static void replay_hud(const GLGame *glgame);
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
  static void spectate(const GLGame *glgame, const GLShip *glship);
  // Online: the identity badge rows, small bottom-row tags like the
  // SPECTATING hint — the peer's name+platform badge ("GLENN - STEAM",
  // verified tick when worker-attested) with the local player's own badge
  // above it, each carrying its pilot's live score (": 4200"). The peer row
  // draws nothing for a legacy (pre-identity) peer — their absence must
  // render exactly the badge-less row; the local row is independent of the
  // peer and skipped only in replay playback.
  static void net_badges(const GLGame *glgame, const GLShip *glship);
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
