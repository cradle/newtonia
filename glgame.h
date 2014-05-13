#ifndef GL_GAME_H
#define GL_GAME_H

#include "state.h"
#include "glship.h"
#include "point.h"
#include "grid.h"
#include "glstarfield.h"
#include "glstation.h"
#include "asteroid.h"
#include <SDL.h>
#include <list>

using namespace std;

class GLGame : public State {
public:
  GLGame(SDL_GameController *controller = NULL);
  GLGame(GLGame const &other);
  virtual ~GLGame();

  void draw();
  void tick(int delta);
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void controller(SDL_Event event);

  friend class Overlay;

  bool cleared() const;

  list<Asteroid*> *objects;

  int num_x_viewports() const;
  int num_y_viewports() const;
private:
  void add_asteroids();
  void toggle_pause();
  void draw_map() const;
  void draw_objects(float direction = 0.0f, bool minimap = false) const;
  void draw_world(GLShip *glship = NULL, bool primary = true) const;
  void draw_perspective(GLShip *glship) const;
  void setup_viewport(bool primary) const;
  void setup_perspective(GLShip *glship) const;
  void setup_orthogonal() const;

  static const int step_size = 10;

  Point world;

  int generation;
  int last_tick, time_until_next_step, num_frames, current_time, time_between_steps;
  int time_until_next_generation;
  bool running, level_cleared, friendly_fire;

  static const int default_world_width, default_world_height;
  static const int default_num_asteroids, extra_num_asteroids;
  unsigned int gameworld, frontstars, rearstars;

  Mix_Chunk *tic_sound = NULL;

  Grid grid;
  GLStarfield *starfield;
  GLStation *station;
  list<GLShip*> *enemies, *players;
};

#endif
