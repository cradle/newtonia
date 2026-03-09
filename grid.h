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
  Object * collide(const Object &object, float proximity = 0.0f) const;

private:
  const list<Object *> &get(int row, int col) const;
  Point cell_size, world_size;
  int num_rows, num_cols;
  vector< vector< list<Object *> > > cells;
};

#endif
