#ifndef GL_GAME_H
#define GL_GAME_H

#include "glship.h"
#include "point.h"
#include "glstarfield.h"
#include <vector>

class GLGame {
public:
  GLGame() {};
  GLGame(float world_width, float world_height);
  ~GLGame();

  void init(int argc, char** argv, float screen_width, float screen_height);
  void run();
  void resize(float x, float y);

  void tick(void);
  void draw(void);
  void keyboard (unsigned char key, int x, int y);
  void keyboard_up (unsigned char key, int x, int y);

private:
  Point window;
  Point world;
  GLStarfield* starfield;
  int last_tick;

  std::vector<GLShip*> objects;
};

#endif
