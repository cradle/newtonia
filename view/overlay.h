#ifndef OVERLAY_H
#define OVERLAY_H

class GLShip;
class GLGame;

class Overlay {
public:  
  static void draw(const GLGame * glgame, GLShip *glship);

private:
  static void score(const GLGame *glgame, GLShip *glship);
  static void level_cleared(const GLGame *glgame);
  static void lives(const GLGame *glgame, GLShip *glship);
  static void weapons(const GLGame *glgame, GLShip *glship);
  static void temperature(const GLGame *glgame, GLShip *glship);
  static void respawn_timer(GLShip *glship);
  static void title_text(const GLGame *glgame);
};

#endif