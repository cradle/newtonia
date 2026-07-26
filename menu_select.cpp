#include "menu_select.h"

#include "typer.h"

namespace MenuSelect {

bool is_up(unsigned char key) { return key == 'w' || key == 'W'; }
bool is_down(unsigned char key) { return key == 's' || key == 'S'; }
bool is_left(unsigned char key) { return key == 'a' || key == 'A'; }
bool is_right(unsigned char key) { return key == 'd' || key == 'D'; }
bool is_confirm(unsigned char key) {
  return key == ' ' || key == '\r' || key == '\n';
}
bool is_back(unsigned char key) { return key == 27; }

bool move_within(unsigned char key, int &sel, int lo, int hi) {
  if (is_up(key)) {
    if (sel > lo) sel--;
    return true;
  }
  if (is_down(key)) {
    if (sel < hi) sel++;
    return true;
  }
  return false;
}

bool move(unsigned char key, int &sel, int count) {
  return move_within(key, sel, 0, count - 1);
}

void draw_row(float y, const std::string &text, float size, bool selected) {
  Typer::draw_centered(0, y, Typer::cursored(text, selected).c_str(), size);
}

void draw_row_cursor(bool selected, float left_x, float right_x, float y,
                     float size) {
  if (!selected) return;
  Typer::draw(left_x, y, '>', size);
  Typer::draw(right_x, y, '<', size);
}

}  // namespace MenuSelect
