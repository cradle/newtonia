#include "grid.h"
#include "point.h"
#include "object.h"
#include <vector>
#include <list>
#include <iostream>
using namespace std;

Grid::Grid(Point size, Point biggest) {
  cell_size = biggest;
  num_rows = int((size.x()*2)/cell_size.x());
  if(cell_size.x()*num_rows < size.x()*2) {
    num_rows += 1;
  }
  num_cols = int((size.y()*2)/cell_size.y());
  if(cell_size.y()*num_rows < size.y()*2) {
    num_cols += 1;
  }
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

void Grid::update(list<Object *> *objects) {
  for(int i = 0; i < num_rows; i++) {
    for(int j = 0; j < num_cols; j++) {
      cells[i][j].clear();
    }
  }
  for(list<Object *>::iterator oi = objects->begin(); oi != objects->end(); oi++) {
    Point p = (*oi)->position;
    int x_cell = p.x()/cell_size.x() + num_rows/2;
    int y_cell = p.y()/cell_size.y() + num_cols/2;
    cells[x_cell][y_cell].push_back(*oi);
  }
}

Grid::~Grid() {
}
