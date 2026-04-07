#include "grid.h"
#include "gl_compat.h"
#include "mesh.h"
#include <algorithm>

void Grid::draw_debug() const {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  static Mesh grid_mesh;
  static Mesh border_mesh;

  MeshBuilder mb;

  // Filled highlight for occupied cells (GL_TRIANGLES, two per quad)
  for(int row = 0; row < num_rows; row++) {
    for(int col = 0; col < num_cols; col++) {
      if(cells[row][col].empty()) continue;
      float intensity = std::min(1.0f, (float)cells[row][col].size() * 0.25f);
      float x0 = row * cell_size.x(), x1 = x0 + cell_size.x();
      float y0 = col * cell_size.y(), y1 = y0 + cell_size.y();
      mb.begin(GL_TRIANGLES);
      mb.color(0.0f, intensity, intensity * 0.5f, 0.15f);
      mb.vertex(x0, y0); mb.vertex(x1, y0); mb.vertex(x1, y1);
      mb.vertex(x0, y0); mb.vertex(x1, y1); mb.vertex(x0, y1);
      mb.end();
    }
  }

  // Interior cell boundary lines — thin, faint cyan
  mb.begin(GL_LINES);
  mb.color(0.0f, 0.8f, 0.8f, 0.3f);
  for(int row = 1; row < num_rows; row++) {
    float x = row * cell_size.x();
    mb.vertex(x, 0); mb.vertex(x, num_cols * cell_size.y());
  }
  for(int col = 1; col < num_cols; col++) {
    float y = col * cell_size.y();
    mb.vertex(0, y); mb.vertex(num_rows * cell_size.x(), y);
  }
  mb.end();

  grid_mesh.upload(mb, GL_STREAM_DRAW);
  glLineWidth(1.0f);
  grid_mesh.draw();

  // Outer boundary — thicker, bright white
  float W = num_rows * cell_size.x();
  float H = num_cols * cell_size.y();
  MeshBuilder mb2;
  mb2.begin(GL_LINE_LOOP);
  mb2.color(1.0f, 1.0f, 1.0f, 0.8f);
  mb2.vertex(0, 0); mb2.vertex(W, 0); mb2.vertex(W, H); mb2.vertex(0, H);
  mb2.end();
  border_mesh.upload(mb2, GL_STREAM_DRAW);
  glLineWidth(3.0f);
  border_mesh.draw();
  glLineWidth(1.0f);
}
