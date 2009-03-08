#ifndef GRID_H
#define GRID_H

#include "point.h"
#include <map>
#include <list>
using namespace std;
class Object;

class Grid {
public:
  Grid(Point size, Point biggest);
  ~Grid();
  
  void display() const;
  void update(const list<Object *> *objects);
  Object * collide(const Object &object, float proximity = 0.0f);
  
private:
  list<Object *> get(Point position, int x, int y);
  Point cell_size;
  int num_rows, num_cols;
  map<Point, list<Object *> > cells;
};

#endif