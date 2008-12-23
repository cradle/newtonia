#ifndef GL_GAME_H
#define GL_GAME_H

#include "state.h"
#include "glship.h"
#include "point.h"
#include "glstarfield.h"
#include "glstation.h"
#include "asteroid.h"
#include <list>

using namespace std;

class GLGame : public State {
public:
  GLGame() {};
  GLGame(int player_count, bool station = false);
  ~GLGame();

  void draw();
  void tick(int delta);
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);

private:
  void toggle_pause();
  bool is_single() const;
  void draw_map() const;
  void draw_objects(bool minimap = false) const;
  void draw_world(GLShip *glship, bool primary) const;
  
  static const int step_size = 10;
  
  Point world;

  int num_players;
  int last_tick, time_until_next_step, num_frames, current_time, time_between_steps;
  bool running;

  static const int default_world_width, default_world_height;
  static const int default_num_asteroids;
  unsigned int gameworld;
  
  GLStarfield *starfield;
  GLStation *station;
  list<GLShip*> *enemies, *players;
  list<Asteroid*> *objects;
};

#endif
