#ifndef OVERLAY_H
#define OVERLAY_H

class GLShip;
class GLGame;

class Overlay {
public:  
  static void draw(const GLGame * glgame, GLShip *glship);

private:
  static void score(GLShip *glship);
  static void level_cleared();
  static void title_text();
  static void lives(GLShip *glship);
  static void weapons(GLShip *glship);
  static void temperature(GLShip *glship);
  static void respawn_timer(GLShip *glship);
  static void title_text(GLShip *glship);
};

#endif