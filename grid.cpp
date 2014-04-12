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

list<Object *> Grid::get(Point position, int x_offset, int y_offset) const {
  int row_num = x_offset + (position.x()/cell_size.x());
  int col_num = y_offset + (position.y()/cell_size.y());
  // cout << "B" << x << "," << y << ": " << "[" << cells.size() << "][" << cells.front().size() << "]" << endl;
  while(row_num < 0)
    row_num += num_rows;
  while(col_num < 0)
    col_num += num_cols;
  while(row_num >= num_rows)
    row_num -= num_rows;
  while(col_num >= num_cols)
    col_num -= num_cols;
  // cout << "A" << x << "," << y << ": " << "[" << cells.size() << "][" << cells.front().size() << "]" << endl;
  return cells[row_num][col_num];
}

Object * Grid::collide(const Object &object, float proximity) const {
  list<Object *> others;
  list<Object *>::iterator o;
  Point offset;
  Object *collided = NULL;
  for(int i = -1; i <= 1 && collided == NULL; i++) {
    for(int j = -1; j <= 1 && collided == NULL; j++) {
      others = get(object.position,i,j);
      for(int x = -1;  x <= 1 && collided == NULL; x++) {
        for(int y = -1; y <= 1 && collided == NULL; y++) {
          offset = Point(-x*world_size.x(), -y*world_size.y());
          for(o = others.begin(); o != others.end() && collided == NULL; o++) {
            if(object.collide(**o, proximity, offset)) {
              collided = *o;
            }
          }
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
