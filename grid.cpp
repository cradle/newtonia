#include "grid.h"
#include "point.h"
#include "object.h"
#include <vector>
#include <list>
#include <math.h>
#include <iostream>
using namespace std;

Grid::Grid(Point size, Point biggest) {
  cell_size = biggest;
  num_rows = int(ceil(size.x()/cell_size.x()));
  num_cols = int(ceil(size.y()/cell_size.y()));
  cout << "Grid: " << num_rows << "x" << num_cols << endl;
  // cells = vector<vector<list<Object *> > >(num_rows);
  // for(int row = 0; row < num_rows; row++) {
  //   cells[row] = vector<list<Object *> >(num_cols);
  // }
}

void Grid::display() const {
  // for(int i = 0; i < num_rows; i++) {
  //   for(int j = 0; j < num_cols; j++) {
  //     if(cells[i][j].size() != 0)
  //       cout << cells[i][j].size();
  //     else
  //       cout << " ";
  //   }
  //   cout << "|";
  // }
  // cout << endl;
}

list<Object *> Grid::get(Point position, int x_offset, int y_offset) {
  WrappedPoint pos = position / cell_size + Point(x_offset, y_offset);
  pos.wrap(num_rows, num_cols);
  return cells[pos];
}

Object * Grid::collide(const Object &object, float proximity) {
  list<Object *> others;
  for(int i = -1; i <= 1; i++) {
    for(int j = -1; j <= 1; j++) {
      others = get(object.position,i,j);
      for(list<Object *>::iterator o = others.begin(); o != others.end(); o++) {
        if(object.collide(**o, proximity)) {
          return *o;
        }
      }
    }
  }
  return NULL;
}

void Grid::update(const list<Object *> *objects) {
  cells.clear();
  Point p;
  list<Object *>::const_iterator oi;
  for(oi = objects->begin(); oi != objects->end(); oi++) {
    //FIX: Shouldn't need *oi check, no null objects should be here
    if(*oi && (*oi)->alive) {
      p = (*oi)->position / cell_size;
      cells[p].push_back(*oi);
    }
  }
}

Grid::~Grid() {
}
