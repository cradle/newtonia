#ifndef GL_GAME_H
#define GL_GAME_H

#include "glship.h"
#include <vector>

class GLGame {
public:
  GLGame() {};
  GLGame(float width, float height);
  ~GLGame();

  void init(int argc, char** argv, float width, float height);
  void run();

  void tick(void);
  void resize(int width, int height);
  void draw(void);
  void keyboard (unsigned char key, int x, int y);
  void keyboard_up (unsigned char key, int x, int y);

private:
  
  void resize_ships(int width, int height);
  //TODO: use Points
  int window_width, window_height;
  int world_width, world_height;
  int last_tick;

  std::vector<GLShip*> objects;
};

#endif
