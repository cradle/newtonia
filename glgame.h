#ifndef GL_GAME_H
#define GL_GAME_H

#include "glship.h"
#include "point.h"
#include "glstarfield.h"
#include "glstation.h"
#include "typer.h"
#include <list>

using namespace std;

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
  void draw_map() const;
  void draw_objects(bool minimap = false) const;
  void draw_world(GLShip *glship, bool primary) const;
  
  static const int step_size = 10;
  
  Point window, world;

  int last_tick, time_until_next_step, num_frames;

  unsigned int gameworld;
  
  Typer* typer;
  
  GLStarfield* starfield;
  GLStation* station;
  list<GLShip*>* enemies;
  list<GLShip*>* players;
};

#endif
