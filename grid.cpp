#include "grid.h"
#include "point.h"
#include "object.h"
#include <vector>
#include <list>
#include <math.h>
#include <algorithm>
#include <iostream>
using namespace std;

Grid::Grid(Point size, Point biggest) {
  cell_size = biggest;
  world_size = size;
  num_rows = int(ceil(size.x()/cell_size.x()));
  num_cols = int(ceil(size.y()/cell_size.y()));
  cout << "Grid: " << num_rows << "x" << num_cols << endl;
  cells = vector<vector<list<Object *> > >(num_rows);
  for(int row = 0; row < num_rows; row++) {
    cells[row] = vector<list<Object *> >(num_cols);
  }
}

void Grid::display() const {
  for(int i = 0; i < num_rows; i++) {
    for(int j = 0; j < num_cols; j++) {
      if(cells[i][j].size() != 0)
        cout << cells[i][j].size();
      else
        cout << " ";
    }
    cout << "|";
  }
  cout << endl;
}

const list<Object *> &Grid::get(int row, int col) const {
  while(row < 0) row += num_rows;
  while(col < 0) col += num_cols;
  while(row >= num_rows) row -= num_rows;
  while(col >= num_cols) col -= num_cols;
  return cells[row][col];
}

Object * Grid::collide(const Object &object, float proximity) const {
  list<Object *>::const_iterator o;
  Object *collided = NULL;

  int base_row = (int)floor(object.position.x() / cell_size.x());
  int base_col = (int)floor(object.position.y() / cell_size.y());

  // An object whose radius + proximity extends past the current cell may collide
  // with objects whose centres are in cells beyond the immediate neighbours.
  // Derive the search range so it scales correctly with any object size or proximity.
  // Formula: (R-1)*cell_size <= object.radius + cell_size/2 + proximity
  //       => R = floor((object.radius + proximity) / cell_size + 0.5) + 1
  float min_cell = std::min(cell_size.x(), cell_size.y());
  int search_range = (int)floor((object.radius + proximity) / min_cell + 0.5f) + 1;

  for(int i = -search_range; i <= search_range && collided == NULL; i++) {
    int row = base_row + i;
    float x_off = (row < 0) ? -world_size.x() : (row >= num_rows) ? world_size.x() : 0.0f;

    for(int j = -search_range; j <= search_range && collided == NULL; j++) {
      int col = base_col + j;
      float y_off = (col < 0) ? -world_size.y() : (col >= num_cols) ? world_size.y() : 0.0f;

      const list<Object *> &others = get(row, col);
      Point offset(x_off, y_off);
      for(o = others.begin(); o != others.end() && collided == NULL; o++) {
        if(object.collide(**o, proximity, offset)) {
          collided = *o;
        }
      }
    }
  }
  return collided;
}

void Grid::update(const list<Object *> *objects) {
  for(int i = 0; i < num_rows; i++) {
    for(int j = 0; j < num_cols; j++) {
      cells[i][j].clear();
    }
  }
  Point p;
  int x,y;
  list<Object *>::const_iterator oi;
  for(oi = objects->begin(); oi != objects->end(); oi++) {
    //FIX: Shouldn't need *oi check, no null objects should be here
    if(*oi && (*oi)->alive) {
      p = (*oi)->position;
      x = p.x()/cell_size.x();
      y = p.y()/cell_size.y();
      cells[x][y].push_back(*oi);
    }
  }
}

Grid::~Grid() {
}
