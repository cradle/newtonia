#ifndef STATE_H
#define STATE_H

#include "point.h"
#include "wrapped_point.h"
#include "typer.h"
#include "glstarfield.h"
#include <SDL.h>

class State {
public:
  State();
  virtual ~State() {};

  virtual void draw(int window_index) = 0;
  virtual void mouse_move(int x, int y);
  virtual void keyboard(unsigned char key, int x, int y) = 0;
  virtual void keyboard_up(unsigned char key, int x, int y) = 0;
  virtual void controller(SDL_Event event) = 0;
  virtual void tick(int delta) = 0;
  virtual void update_lists();
  bool is_finished();
  State* get_next_state();

  virtual void resize(int width, int height);

protected:
  void request_state_change(State* next_state);
  Point window;

private:
  bool finished;
  State* next_state;
};

#endif
