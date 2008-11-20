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

  void tick(void);
  void resize(int world_width, int world_height);
  void draw(void);
  void keyboard (unsigned char key, int x, int y);
  void keyboard_up (unsigned char key, int x, int y);

private:
  
  void resize_ships(int width, int height);
  Point window;
  Point world;
  GLStarfield* starfield;
  int last_tick;

  std::vector<GLShip*> objects;
};

#endif
