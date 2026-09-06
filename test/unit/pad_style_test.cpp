// Unit test for the pure half of pad_style.h — the button-label table and
// the classifiers. Links nothing (no SDL runtime, no GL):
//
//   g++ -std=c++11 -I. -I/usr/include/SDL2 test/unit/pad_style_test.cpp -o /tmp/pad_style_test && /tmp/pad_style_test
//
// Lives under test/unit/, NOT test/: the Makefile's source glob is
// `*/*.cpp`, so a .cpp directly under test/ links into the game and
// collides on main().
//
// The runtime half (the per-pad cache, the Steam Input query) needs a
// real pad and is field-verified; NEWTONIA_PAD_STYLE=ps5 forces the
// vocabulary for a screenshot pass without one.

#include <cstdio>
#include <cstring>

#include "../../pad_style.h"

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { fails++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); \
  if (strcmp(_a, _b) != 0) { fails++; \
  fprintf(stderr, "FAIL %s:%d: %s == \"%s\", want \"%s\"\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

int main() {
  // --- Xbox vocabulary: letters, bumpers, START/BACK ---
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_A), "A");
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_B), "B");
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_X), "X");
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_Y), "Y");
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_LEFTSHOULDER), "LB");
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), "RB");
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_START), "START");
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_BACK), "BACK");
  CHECK_STR(pad_button_label(PAD_STYLE_XBOX, SDL_CONTROLLER_BUTTON_LEFTSTICK), "LEFT STICK BUTTON");

  // --- PlayStation vocabulary: shape glyphs, L1/R1/L3/R3, OPTIONS ---
  CHECK_STR(pad_button_label(PAD_STYLE_PS4, SDL_CONTROLLER_BUTTON_A), PAD_GLYPH_CROSS);
  CHECK_STR(pad_button_label(PAD_STYLE_PS4, SDL_CONTROLLER_BUTTON_B), PAD_GLYPH_CIRCLE);
  CHECK_STR(pad_button_label(PAD_STYLE_PS4, SDL_CONTROLLER_BUTTON_X), PAD_GLYPH_SQUARE);
  CHECK_STR(pad_button_label(PAD_STYLE_PS4, SDL_CONTROLLER_BUTTON_Y), PAD_GLYPH_TRIANGLE);
  CHECK_STR(pad_button_label(PAD_STYLE_PS5, SDL_CONTROLLER_BUTTON_LEFTSHOULDER), "L1");
  CHECK_STR(pad_button_label(PAD_STYLE_PS5, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), "R1");
  CHECK_STR(pad_button_label(PAD_STYLE_PS5, SDL_CONTROLLER_BUTTON_LEFTSTICK), "L3");
  CHECK_STR(pad_button_label(PAD_STYLE_PS5, SDL_CONTROLLER_BUTTON_RIGHTSTICK), "R3");
  CHECK_STR(pad_button_label(PAD_STYLE_PS4, SDL_CONTROLLER_BUTTON_START), "OPTIONS");
  CHECK_STR(pad_button_label(PAD_STYLE_PS5, SDL_CONTROLLER_BUTTON_START), "OPTIONS");
  // The one PS4/PS5 difference: the share button's print.
  CHECK_STR(pad_button_label(PAD_STYLE_PS4, SDL_CONTROLLER_BUTTON_BACK), "SHARE");
  CHECK_STR(pad_button_label(PAD_STYLE_PS5, SDL_CONTROLLER_BUTTON_BACK), "CREATE");

  // --- draw_button's contract: face buttons are ONE char in every style,
  // nothing else is (so a word never gets circled) ---
  static const SDL_GameControllerButton faces[] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y };
  static const PadStyle styles[] = { PAD_STYLE_XBOX, PAD_STYLE_PS4, PAD_STYLE_PS5 };
  for (size_t s = 0; s < 3; s++) {
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
      const char *label = pad_button_label(styles[s], (SDL_GameControllerButton)b);
      bool face = false;
      for (size_t f = 0; f < 4; f++) if (faces[f] == b) face = true;
      CHECK((strlen(label) == 1) == face);
      CHECK(strlen(label) >= 1);
      // The shape glyphs are control bytes below any printable character,
      // and only the PlayStation face buttons produce them.
      bool shape = strlen(label) == 1 && pad_glyph_is_shape(label[0]);
      CHECK(shape == (face && pad_style_is_playstation(styles[s])));
    }
  }
  // The circle cue: letters and shapes, never a word, never the "-"
  // unbound marker.
  CHECK(pad_label_is_face("A") && pad_label_is_face(PAD_GLYPH_CROSS));
  CHECK(!pad_label_is_face("-") && !pad_label_is_face("LB") && !pad_label_is_face(""));

  // Shape glyphs are outside the peer-name whitelist by construction:
  // control bytes, never printable.
  CHECK(PAD_GLYPH_CROSS_C < ' ' && PAD_GLYPH_TRIANGLE_C < ' ');
  CHECK(!pad_glyph_is_shape('A') && !pad_glyph_is_shape('\x01'));

  // --- SDL type classifier ---
#if SDL_VERSION_ATLEAST(2, 0, 14)
  CHECK(pad_style_from_sdl_type(SDL_CONTROLLER_TYPE_PS4) == PAD_STYLE_PS4);
  CHECK(pad_style_from_sdl_type(SDL_CONTROLLER_TYPE_PS5) == PAD_STYLE_PS5);
  CHECK(pad_style_from_sdl_type(SDL_CONTROLLER_TYPE_XBOX360) == PAD_STYLE_XBOX);
  CHECK(pad_style_from_sdl_type(SDL_CONTROLLER_TYPE_XBOXONE) == PAD_STYLE_XBOX);
  CHECK(pad_style_from_sdl_type(SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO) == PAD_STYLE_XBOX);
  CHECK(pad_style_from_sdl_type(SDL_CONTROLLER_TYPE_VIRTUAL) == PAD_STYLE_XBOX);
  CHECK(pad_style_from_sdl_type(SDL_CONTROLLER_TYPE_UNKNOWN) == PAD_STYLE_XBOX);
#endif
#if SDL_VERSION_ATLEAST(2, 0, 16)
  CHECK(pad_style_from_sdl_type(SDL_CONTROLLER_TYPE_PS3) == PAD_STYLE_PS4);
#endif

  // --- name classifier (the UNKNOWN-type fallback) ---
  CHECK(pad_style_from_name("DualSense Wireless Controller") == PAD_STYLE_PS5);
  CHECK(pad_style_from_name("Sony DualSense Edge") == PAD_STYLE_PS5);
  CHECK(pad_style_from_name("PS4 Controller") == PAD_STYLE_PS4);
  CHECK(pad_style_from_name("Wireless Controller") == PAD_STYLE_PS4);   // the DS4's HID name
  CHECK(pad_style_from_name("Sony Interactive Entertainment DualShock 4") == PAD_STYLE_PS4);
  CHECK(pad_style_from_name("PLAYSTATION(R)3 Controller") == PAD_STYLE_PS4);
  CHECK(pad_style_from_name("Xbox Wireless Controller") == PAD_STYLE_XBOX);
  CHECK(pad_style_from_name("Wireless Controller (Xbox)") == PAD_STYLE_XBOX);
  CHECK(pad_style_from_name("wireless controller") == PAD_STYLE_PS4);
  CHECK(pad_style_from_name("Steam Virtual Gamepad") == PAD_STYLE_XBOX);
  CHECK(pad_style_from_name("Microsoft X-Box 360 pad") == PAD_STYLE_XBOX);
  CHECK(pad_style_from_name("Pro Controller") == PAD_STYLE_XBOX);
  CHECK(pad_style_from_name("") == PAD_STYLE_XBOX);
  CHECK(pad_style_from_name(NULL) == PAD_STYLE_XBOX);

  // --- NEWTONIA_PAD_STYLE override parser ---
  PadStyle out = PAD_STYLE_XBOX;
  CHECK(pad_style_parse("ps4", &out) && out == PAD_STYLE_PS4);
  CHECK(pad_style_parse("ps5", &out) && out == PAD_STYLE_PS5);
  CHECK(pad_style_parse("playstation", &out) && out == PAD_STYLE_PS5);
  CHECK(pad_style_parse("xbox", &out) && out == PAD_STYLE_XBOX);
  CHECK(!pad_style_parse("dualsense", &out));
  CHECK(!pad_style_parse("", &out));
  CHECK(!pad_style_parse(NULL, &out));

  CHECK_STR(pad_style_name(PAD_STYLE_PS5), "ps5");
  CHECK_STR(pad_style_name(PAD_STYLE_XBOX), "xbox");

  if (fails) { fprintf(stderr, "pad_style_test: %d failure(s)\n", fails); return 1; }
  printf("pad_style_test: PASS\n");
  return 0;
}
