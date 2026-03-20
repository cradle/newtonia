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
  cells = vector<vector<vector<Object *>>>(num_rows, vector<vector<Object *>>(num_cols));
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

const vector<Object *> &Grid::get(int row, int col) const {
  while(row < 0) row += num_rows;
  while(col < 0) col += num_cols;
  while(row >= num_rows) row -= num_rows;
  while(col >= num_cols) col -= num_cols;
  return cells[row][col];
}

Object * Grid::collide(const Object &object, float proximity, bool skip_invincible) const {
  vector<Object *>::const_iterator o;
  Object *collided = NULL;

  // Query cells the object's effective radius spans, plus one cell on each
  // side. The ±1 allows row/col to go out of bounds, which triggers the
  // world-wrap offset below — essential for collisions across world edges.
  float r = object.radius + proximity;
  int row_min = (int)floor((object.position.x() - r) / cell_size.x()) - 1;
  int row_max = (int)floor((object.position.x() + r) / cell_size.x()) + 1;
  int col_min = (int)floor((object.position.y() - r) / cell_size.y()) - 1;
  int col_max = (int)floor((object.position.y() + r) / cell_size.y()) + 1;

  for(int row = row_min; row <= row_max && collided == NULL; row++) {
    float x_off = (row < 0) ? -world_size.x() : (row >= num_rows) ? world_size.x() : 0.0f;

    for(int col = col_min; col <= col_max && collided == NULL; col++) {
      float y_off = (col < 0) ? -world_size.y() : (col >= num_cols) ? world_size.y() : 0.0f;

      const vector<Object *> &others = get(row, col);
      Point offset(x_off, y_off);
      for(o = others.begin(); o != others.end() && collided == NULL; o++) {
        if(skip_invincible && (*o)->invincible) continue;
        if(object.collide(**o, proximity, offset)) {
          // Narrow phase: for exact tests (proximity==0) refine with polygon
          if(proximity == 0.0f && !(*o)->contains(object.position - offset, object.radius)) continue;
          collided = *o;
        }
      }
    }
  }
  return collided;
}

void Grid::update(const list<Object *> *objects) {
  for(int i = 0; i < num_rows; i++)
    for(int j = 0; j < num_cols; j++)
      cells[i][j].clear();

  list<Object *>::const_iterator oi;
  for(oi = objects->cbegin(); oi != objects->cend(); oi++) {
    if(!(*oi) || !(*oi)->alive) continue;
    Point p = (*oi)->position;
    float r = (*oi)->radius;
    // Insert into every cell the object's body overlaps (clamped to grid).
    // World-wrap collisions are handled at query time via out-of-bounds offsets.
    int x_min = std::max(0, (int)floor((p.x() - r) / cell_size.x()));
    int x_max = std::min(num_rows - 1, (int)floor((p.x() + r) / cell_size.x()));
    int y_min = std::max(0, (int)floor((p.y() - r) / cell_size.y()));
    int y_max = std::min(num_cols - 1, (int)floor((p.y() + r) / cell_size.y()));
    for(int x = x_min; x <= x_max; x++)
      for(int y = y_min; y <= y_max; y++)
        cells[x][y].push_back(*oi);
  }
}

Grid::~Grid() {
}
