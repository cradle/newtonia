#ifndef MENU_SELECT_H
#define MENU_SELECT_H

#include <string>

// Shared primitives for every screen that highlights a row: the main menu,
// the options and replays lists, the Yes/No confirms, the lobby's HOST/JOIN
// chooser and LAN host list, and the in-game pause menu.
//
// The nav ladders all speak the same logical keys already — State::nav_key
// turns arrows into WASD, State::nav_key_from_controller turns a pad's dpad,
// stick and face buttons into the same set — so the tests below are the
// whole vocabulary. Movement and the cursor live here too: a screen that
// hand-rolls either is a screen that drifts from the rest, which is how the
// lone "> " prefix survived on two lists after the menus had moved on.
namespace MenuSelect {

// Logical key tests. Letters match in both cases — the desktop entry point
// delivers whatever the shift state produced.
bool is_up(unsigned char key);
bool is_down(unsigned char key);
bool is_left(unsigned char key);
bool is_right(unsigned char key);
bool is_confirm(unsigned char key);  // Enter, newline, space (pad A and
                                     // Start arrive already translated)
bool is_back(unsigned char key);     // Esc (pad B/Back arrives as Esc)

// Move `sel` for an up/down key, clamped to [lo, hi]. Returns true when the
// key was a movement, so a ladder can stop looking. No wrapping: every
// screen clamped before this existed, and a menu that jumps from the last
// row to the first loses the eye.
bool move_within(unsigned char key, int &sel, int lo, int hi);
// The common case — a plain list of `count` rows indexed from zero.
bool move(unsigned char key, int &sel, int count);

// A centred selectable row: "> ITEM <" when selected, "  ITEM  " when not.
// Both forms pad symmetrically, so the item's own glyphs sit on the centre
// line either way and the label doesn't shift as the highlight moves.
void draw_row(float y, const std::string &text, float size, bool selected);

// The cursor for a multi-column row (the options and replays lists): '>' at
// left_x and '<' at right_x, flanking the whole row, and nothing at all when
// the row isn't selected — so the columns keep their x. Such a row has no
// single word to wrap; hugging the first column would slide the closing mark
// sideways on every selection move.
void draw_row_cursor(bool selected, float left_x, float right_x, float y,
                     float size);

}  // namespace MenuSelect

#endif
