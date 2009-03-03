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
  void update(list<Object *> *objects);
  
private:
  Point cell_size;
  int num_rows, num_cols;
  vector< vector< list<Object *> > > cells;
};

#endif