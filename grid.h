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
  // Every object whose cell footprint could touch `object` (broad phase
  // only — the caller does its own exact test). Duplicates are possible
  // when an object spans several of the queried cells, and `object`
  // itself may be included; both are the caller's to filter. Used by the
  // elastic-asteroid pass, which otherwise tested every rock against
  // every other one each step.
  void query_neighbours(const Object &object, vector<Object *> &out) const;
  // Broad-phase gather of everything within `radius` of `center`: the
  // cells overlapping the circle's bounding box, wrap included. The seeks'
  // candidate source (seek.h) — cells are sized to the biggest asteroid
  // and the cell COUNT grows with the world, so at late generations a
  // weapon-range circle touches a small fraction of the grid where a list
  // walk touched every rock alive. Same caveats as query_neighbours:
  // duplicates possible, no exact test — the caller measures real
  // (wrapped) distances itself.
  void query_radius(Point center, float radius, vector<Object *> &out) const;

private:
  const vector<Object *> &get(int row, int col) const;
  Point cell_size, world_size;
  int num_rows, num_cols;
  vector< vector< vector<Object *> > > cells;
};

#endif
