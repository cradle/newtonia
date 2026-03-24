#ifndef GRID_H
#define GRID_H

#include "point.h"
#include <vector>
#include <list>
using namespace std;
class Object;

class Grid {
public:
  Grid(Point size, Point biggest);
  ~Grid();

  void display() const;
  void draw_debug() const;
  void update(const list<Object *> *objects);
  Object * collide(const Object &object, float proximity = 0.0f, bool skip_invincible = false) const;
  void query_segment(Point a, Point b, vector<Object *> &out) const;

private:
  const vector<Object *> &get(int row, int col) const;
  Point cell_size, world_size;
  int num_rows, num_cols;
  vector< vector< vector<Object *> > > cells;
};

#endif
