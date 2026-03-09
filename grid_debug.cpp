#include "grid.h"
#include "gl_compat.h"
#include <algorithm>

void Grid::draw_debug() const {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Filled highlight for occupied cells
  glBegin(GL_QUADS);
  for(int row = 0; row < num_rows; row++) {
    for(int col = 0; col < num_cols; col++) {
      if(cells[row][col].empty()) continue;
      float intensity = std::min(1.0f, (float)cells[row][col].size() * 0.25f);
      glColor4f(0.0f, intensity, intensity * 0.5f, 0.15f);
      float x0 = row * cell_size.x(), x1 = x0 + cell_size.x();
      float y0 = col * cell_size.y(), y1 = y0 + cell_size.y();
      glVertex2f(x0, y0); glVertex2f(x1, y0);
      glVertex2f(x1, y1); glVertex2f(x0, y1);
    }
  }
  glEnd();

  // Interior cell boundary lines — thin, faint cyan
  glLineWidth(1.0f);
  glColor4f(0.0f, 0.8f, 0.8f, 0.3f);
  glBegin(GL_LINES);
  for(int row = 1; row < num_rows; row++) {
    float x = row * cell_size.x();
    glVertex2f(x, 0);
    glVertex2f(x, num_cols * cell_size.y());
  }
  for(int col = 1; col < num_cols; col++) {
    float y = col * cell_size.y();
    glVertex2f(0, y);
    glVertex2f(num_rows * cell_size.x(), y);
  }
  glEnd();

  // Outer boundary — thicker, bright white
  float W = num_rows * cell_size.x();
  float H = num_cols * cell_size.y();
  glLineWidth(3.0f);
  glColor4f(1.0f, 1.0f, 1.0f, 0.8f);
  glBegin(GL_LINE_LOOP);
  glVertex2f(0, 0); glVertex2f(W, 0);
  glVertex2f(W, H); glVertex2f(0, H);
  glEnd();
  glLineWidth(1.0f);
}
