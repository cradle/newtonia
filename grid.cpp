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

list<Object *> Grid::get(WrappedPoint position, int x_offset, int y_offset, Point &offset) const {
  position /= cell_size;
  position += Point(x_offset, y_offset); 
  position.wrap_to(0,0,num_rows,num_cols);
  return cells[position.x()][position.y()];
}

Object * Grid::collide(Object &object) const {
  list<Object *> others;
  for(int i = -1; i <= 1; i++) {
    for(int j = -1; j <= 1; j++) {
      others = get(object.position,i,j);
      for(list<Object *>::iterator o = others.begin(); o != others.end(); o++) {
        if(object.collide(*o)) {
          return *o;
        }
      }
    }
  }
  return NULL;
}

void Grid::update(list<Object *> *objects) {
  for(int i = 0; i < num_rows; i++) {
    for(int j = 0; j < num_cols; j++) {
      cells[i][j].clear();
    }
  }
  Point p;
  int x,y;
  for(list<Object *>::iterator oi = objects->begin(); oi != objects->end(); oi++) {
    if((*oi)->alive) {
      p = (*oi)->position;
      x = p.x()/cell_size.x();
      y = p.y()/cell_size.y();
      cells[x][y].push_back(*oi);
    }
  }
}

Grid::~Grid() {
}
