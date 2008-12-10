#ifndef GL_GAME_H
#define GL_GAME_H

#include "state.h"
#include "glship.h"
#include "point.h"
#include "glstarfield.h"
#include "glstation.h"
#include "world.h"
#include <list>

using namespace std;

class GLGame : public State {
public:
  GLGame() {};
  GLGame(float world_width, float world_height, int player_count);
  ~GLGame();

  void draw();
  void tick(int delta);
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);

private:  
  World game_world;
};

#endif
