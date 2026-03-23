#ifndef OVERLAY_H
#define OVERLAY_H

class GLShip;
class GLGame;

class Overlay {
public:
  static const float CORNER_INSET;
  static void draw(const GLGame * glgame, const GLShip *glship);

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
  static void touch_controls(const GLGame *glgame, const GLShip *glship);
  static void edge_indicators(const GLGame *glgame, const GLShip *glship);
  static void draw_circle(float cx, float cy, float r, int segs, bool filled);
};

#endif
