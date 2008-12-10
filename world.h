#ifndef WORLD_H
#define WORLD_H

#include "point.h"

class World {
public:
  World(int width, int height, int num_players = 1);
  ~World();
  
  void tick(int delta);
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void draw(GLShip *glship) const;
  void draw(bool minimap = false);
  
private:
  void toggle_pause();
  bool is_single() const;
  void draw_map() const;
  void draw_objects(bool minimap = false) const;
  void draw_world(GLShip *glship, bool primary) const;
  
  static const int step_size = 10;
  Point size;
  int num_players;
  int last_tick, time_until_next_step, num_frames, current_time, time_between_steps;
  bool running;

  unsigned int gameworld;
  GLStarfield *starfield;
  GLStation *station;
  list<GLShip*> *enemies, *players;
};

#endif