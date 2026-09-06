#ifndef PAD_STYLE_H
#define PAD_STYLE_H

// Controller glyph vocabulary — which family of button labels a pad's
// hints should use. Every on-screen mention of a pad button (the F1 help
// card, the weapon rows' FIRE/NEXT chips, the pause/resume/join/boost
// hints, the lobby's code-entry hints) asks here instead of spelling out
// "A" or "START": a DualShock/DualSense pilot reads cross / circle /
// square / triangle, L1/R1, OPTIONS and SHARE/CREATE, and an Xbox (or
// Deck, or Switch — SDL's default button-label mapping already names
// Nintendo pads by their printed letters) pilot keeps the letters.
//
// Classification (pad.cpp, behind the pad seam): the NEWTONIA_PAD_STYLE
// override first (dev/screenshot hook — no pad needed to see the
// PlayStation card), then the backend's own type — SDL's
// (vendor/product-derived; SDL >= 2.30 also reads Steam's description of a
// Steam-Input-emulated pad, so a DualSense behind Steam Input still
// classifies as one — the depot links 2.32 statically) or Steam Input's
// ESteamInputType — then the device name. Cached per PadId — ids are never
// reused within a process.
//
// This header is the pure VOCABULARY only: (style, position) -> label.
// Which position a hint should name is the pad seam's business
// (pad_action_label in pad.h): for an SDL pad it is the position the
// game hard-codes, for a Steam Input pad it is whatever the player's
// current layout binds the ACTION to (STEAMINPUT.md §4). Labels follow the
// pad's TYPE for the glyph set and the LAYOUT for the position.
//
// The PlayStation face glyphs are Typer CONTROL BYTES (like
// Typer::VERIFIED_TICK), so a label can ride an ordinary hint string
// ("\x03 MENU") and Typer::draw_button can circle it like a letter. They
// are deliberately outside net_name_char_drawable's whitelist and are
// stripped by net_sanitize_name, so a peer's display name can never carry
// one.

#include <SDL.h>

enum PadStyle {
  PAD_STYLE_XBOX = 0,  // letters: A B X Y, LB RB, START BACK — the default
  PAD_STYLE_PS4  = 1,  // shapes, L1 R1, OPTIONS SHARE
  PAD_STYLE_PS5  = 2,  // shapes, L1 R1, OPTIONS CREATE
};

// Typer glyph slots for the PlayStation face buttons (see the header
// comment). Single-byte strings so they concatenate into hint text.
#define PAD_GLYPH_CROSS    "\x02"
#define PAD_GLYPH_CIRCLE   "\x03"
#define PAD_GLYPH_SQUARE   "\x04"
#define PAD_GLYPH_TRIANGLE "\x05"
static const char PAD_GLYPH_CROSS_C    = '\x02';
static const char PAD_GLYPH_CIRCLE_C   = '\x03';
static const char PAD_GLYPH_SQUARE_C   = '\x04';
static const char PAD_GLYPH_TRIANGLE_C = '\x05';
inline bool pad_glyph_is_shape(char c) {
  return c >= PAD_GLYPH_CROSS_C && c <= PAD_GLYPH_TRIANGLE_C;
}

inline bool pad_style_is_playstation(PadStyle s) {
  return s == PAD_STYLE_PS4 || s == PAD_STYLE_PS5;
}

#include <cctype>
#include <cstring>

// --- Pure part: labels and classifiers. Header-inline so
// test/unit/pad_style_test.cpp can exercise them with no SDL runtime linked. ---

// Positions beyond SDL's button enum that a hint can still name: the
// triggers and the sticks, which SDL reports as axes. Numbered past every
// SDL_CONTROLLER_BUTTON_* value (the enum stops at 21 in SDL 2.32) so an
// int carrying either vocabulary never collides. Steam Input layouts bind
// actions to them freely (fire on RT alone, steer on the right stick), so
// the label table has to speak them; on the SDL path the game never asks
// for them.
enum PadPseudoButton {
  PAD_BUTTON_LEFT_TRIGGER  = 64,
  PAD_BUTTON_RIGHT_TRIGGER = 65,
  PAD_BUTTON_LEFT_STICK    = 66,  // the stick's MOVE, not its click
  PAD_BUTTON_RIGHT_STICK   = 67,
  // The two pad zoom steps (STEAMINPUT.md §2: new actions, no hard-coded
  // pad position). Synthesized as SDL button events on these values, which
  // are SDL's PADDLE1/PADDLE2 where the SDL is new enough to have them —
  // so an Elite pad's paddles zoom on the SDL path for free — and inert
  // numbers on an older SDL, where nothing emits them.
#if SDL_VERSION_ATLEAST(2, 0, 14)
  PAD_BUTTON_ZOOM_IN  = SDL_CONTROLLER_BUTTON_PADDLE1,
  PAD_BUTTON_ZOOM_OUT = SDL_CONTROLLER_BUTTON_PADDLE2,
#else
  PAD_BUTTON_ZOOM_IN  = 15,
  PAD_BUTTON_ZOOM_OUT = 16,
#endif
};

// Label for a button (an SDL_GameControllerButton or a PadPseudoButton) in
// a style. Face buttons (and only face buttons) come back as a
// ONE-character string — a letter or a shape glyph — which is the contract
// Typer::draw_button relies on to decide what to circle; everything else
// is a word ("L1", "OPTIONS", "DPAD UP"). Never NULL.
inline const char *pad_button_label(PadStyle s, int b) {
  bool ps = pad_style_is_playstation(s);
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A:             return ps ? PAD_GLYPH_CROSS    : "A";
    case SDL_CONTROLLER_BUTTON_B:             return ps ? PAD_GLYPH_CIRCLE   : "B";
    case SDL_CONTROLLER_BUTTON_X:             return ps ? PAD_GLYPH_SQUARE   : "X";
    case SDL_CONTROLLER_BUTTON_Y:             return ps ? PAD_GLYPH_TRIANGLE : "Y";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return ps ? "L1" : "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return ps ? "R1" : "RB";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return ps ? "L3" : "LEFT STICK BUTTON";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return ps ? "R3" : "RIGHT STICK BUTTON";
    case SDL_CONTROLLER_BUTTON_START:         return ps ? "OPTIONS" : "START";
    case SDL_CONTROLLER_BUTTON_BACK:
      return s == PAD_STYLE_PS5 ? "CREATE" : ps ? "SHARE" : "BACK";
    case SDL_CONTROLLER_BUTTON_GUIDE:         return ps ? "PS" : "GUIDE";
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "DPAD UP";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "DPAD DOWN";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "DPAD LEFT";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "DPAD RIGHT";
    case PAD_BUTTON_LEFT_TRIGGER:             return ps ? "L2" : "LT";
    case PAD_BUTTON_RIGHT_TRIGGER:            return ps ? "R2" : "RT";
    case PAD_BUTTON_LEFT_STICK:               return "LEFT STICK";
    case PAD_BUTTON_RIGHT_STICK:              return "RIGHT STICK";
    case PAD_BUTTON_ZOOM_IN:                  return "PADDLE 1";
    case PAD_BUTTON_ZOOM_OUT:                 return "PADDLE 2";
    // Touchpad, misc, the other paddles: never hinted; a WORD so it is
    // never circled.
    default:                                  return "BUTTON";
  }
}

// Typer::draw_button's cue: a face-button label — ONE character that is a
// letter or a shape glyph. The hint sites circle exactly these; a word
// ("LB", "LEFT STICK") and the one-character UNBOUND marker "-" (a Steam
// layout that binds nothing to the action) draw as plain text — the
// marker drew as a circled dash before this existed (field, 2026-09-06).
inline bool pad_label_is_face(const char *s) {
  return s && s[0] && !s[1] &&
         (pad_glyph_is_shape(s[0]) || isalnum((unsigned char)s[0]));
}

// The name to log beside a pad ("xbox", "ps4", "ps5").
inline const char *pad_style_name(PadStyle s) {
  switch (s) {
    case PAD_STYLE_PS4: return "ps4";
    case PAD_STYLE_PS5: return "ps5";
    default:            return "xbox";
  }
}

// From an SDL_GameControllerType value (int so a pre-2.0.12 SDL, which
// has no such enum, still compiles): PS3/PS4 -> PS4, PS5 -> PS5, anything
// else — Xbox, Switch, generic, virtual, unknown — -> Xbox.
inline PadStyle pad_style_from_sdl_type(int sdl_type) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
  switch (sdl_type) {
    case SDL_CONTROLLER_TYPE_PS4: return PAD_STYLE_PS4;
#if SDL_VERSION_ATLEAST(2, 0, 14)
    case SDL_CONTROLLER_TYPE_PS5: return PAD_STYLE_PS5;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 16)
    case SDL_CONTROLLER_TYPE_PS3: return PAD_STYLE_PS4;
#endif
    default: break;
  }
#else
  (void)sdl_type;
#endif
  return PAD_STYLE_XBOX;
}

inline bool pad_style_contains_ci(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  for (; *hay; hay++) {
    size_t i = 0;
    while (i < n && hay[i] &&
           tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
      i++;
    if (i == n) return true;
  }
  return false;
}

inline bool pad_style_equals_ci(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}

// From the device name, for SDL builds/backends that report UNKNOWN:
// "DualSense" -> PS5; "DualShock", "PS4", "PS3", "PlayStation", "Sony"
// anywhere in the name, or EXACTLY "Wireless Controller" (the DS4's own
// bare HID name — a substring match would also take "Xbox Wireless
// Controller") -> PS4; else Xbox.
inline PadStyle pad_style_from_name(const char *name) {
  if (!name) return PAD_STYLE_XBOX;
  if (pad_style_contains_ci(name, "dualsense")) return PAD_STYLE_PS5;
  static const char *const ps4_marks[] = {
    "dualshock", "ps4", "ps3", "playstation", "sony",
  };
  for (size_t i = 0; i < sizeof(ps4_marks) / sizeof(ps4_marks[0]); i++)
    if (pad_style_contains_ci(name, ps4_marks[i])) return PAD_STYLE_PS4;
  if (pad_style_equals_ci(name, "Wireless Controller")) return PAD_STYLE_PS4;
  return PAD_STYLE_XBOX;
}

// From the NEWTONIA_PAD_STYLE value ("xbox", "ps4", "ps5",
// "playstation" = ps5). false when the string names no style.
inline bool pad_style_parse(const char *s, PadStyle *out) {
  if (!s || !out) return false;
  if (!strcmp(s, "xbox"))        { *out = PAD_STYLE_XBOX; return true; }
  if (!strcmp(s, "ps4"))         { *out = PAD_STYLE_PS4;  return true; }
  if (!strcmp(s, "ps5"))         { *out = PAD_STYLE_PS5;  return true; }
  if (!strcmp(s, "playstation")) { *out = PAD_STYLE_PS5;  return true; }
  return false;
}

// The runtime half — per-pad classification, the cache, pad_style_any —
// lives behind the pad seam (pad.h), which is also where a hint asks which
// POSITION an action sits on.

#endif
