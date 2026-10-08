#ifndef KEYBOARD_SEEN_H
#define KEYBOARD_SEEN_H

// Whether this run has seen a real keyboard key. The entry points mark it
// on a hardware key event only — never on the keys they synthesize for
// touch or pads — and the HUD's keyboard hints, the roster's keyboard
// rows and the seats a CONTINUE binds to keyboard clusters all wait for
// it: a pad-only player (Steam Deck, Xbox) was told "show controls with
// F8" on a seat no keyboard could reach (field, Xbox, 2026-10-08).
// Per run, not saved: a keyboard unplugged since last time should not
// keep its hints.
inline bool &keyboard_seen_flag() { static bool seen = false; return seen; }
inline void note_keyboard_used() { keyboard_seen_flag() = true; }
inline bool keyboard_seen() { return keyboard_seen_flag(); }

#endif
