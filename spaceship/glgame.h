#ifndef GL_GAME_H
#define GL_GAME_H

#include "glship.h"
#include "point.h"
#include "glstarfield.h"
#include "glstation.h"
#include "typer.h"
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
  void draw_objects(bool minimap = false);
  void keyboard (unsigned char key, int x, int y);
  void keyboard_up (unsigned char key, int x, int y);

private:
  Point window;
  Point world;
  GLStarfield* starfield;
  GLStation* station;
  int last_tick, time_until_next_step, num_frames;
  static const int step_size = 10;

  unsigned int gameworld;

  void draw_map();
  void draw_world(GLShip *glship, bool primary);
  
  Typer* typer;

  std::vector<GLShip*>* enemies;
  std::vector<GLShip*>* players;
};

#endif
