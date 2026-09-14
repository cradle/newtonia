// Unit test for the pad action vocabulary (pad.h) against the Steam Input
// manifest (steam/game_actions_4536720.vdf). Links nothing — the table is
// header-inline — and reads the manifest as text. Run from the repo root:
//
//   g++ -std=c++11 -I. -I/usr/include/SDL2 test/unit/pad_actions_test.cpp -o /tmp/pad_actions_test && /tmp/pad_actions_test
//
// What it pins (STEAMINPUT.md §2–§3):
//  - every action the game reads by name is declared in the manifest, in
//    its own set's block, under the right kind (StickPadGyro / Button),
//    with a localization string — a rename on either side fails here, not
//    in the field as an action that never fires;
//  - every manifest action is one the game knows (no dead declarations);
//  - within a set, digital actions synthesize DISTINCT SDL buttons, so a
//    synthesized event is unambiguous to the consumers;
//  - the SDL-path positions are the ones the consumers switch on (the
//    "today's Xbox binding" column read right-to-left).

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "../../pad.h"

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { fails++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECKF(cond, ...) do { if (!(cond)) { fails++; \
  fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\n"); } } while (0)

// The text of one top-level set block ("Ship" { ... }) inside "actions".
static std::string set_block(const std::string &vdf, const char *set) {
  std::string key = std::string("\"") + set + "\"";
  size_t actions = vdf.find("\"actions\"");
  if (actions == std::string::npos) return "";
  size_t at = vdf.find(key, actions);
  if (at == std::string::npos) return "";
  size_t open = vdf.find('{', at);
  if (open == std::string::npos) return "";
  int depth = 0;
  for (size_t i = open; i < vdf.size(); i++) {
    if (vdf[i] == '{') depth++;
    else if (vdf[i] == '}' && --depth == 0) return vdf.substr(open, i - open + 1);
  }
  return "";
}

// The sub-block ("Button" { ... }) of a set block.
static std::string kind_block(const std::string &set, const char *kind) {
  std::string key = std::string("\"") + kind + "\"";
  size_t at = set.find(key);
  if (at == std::string::npos) return "";
  size_t open = set.find('{', at);
  int depth = 0;
  for (size_t i = open; i < set.size(); i++) {
    if (set[i] == '{') depth++;
    else if (set[i] == '}' && --depth == 0) return set.substr(open, i - open + 1);
  }
  return "";
}

static bool has_key(const std::string &block, const char *name) {
  return block.find(std::string("\"") + name + "\"") != std::string::npos;
}

// Every quoted key that opens a line in a Button block: `"fire" "#..."`.
static std::set<std::string> button_keys(const std::string &block) {
  std::set<std::string> out;
  std::istringstream in(block);
  std::string line;
  while (std::getline(in, line)) {
    size_t q = line.find('"');
    if (q == std::string::npos) continue;
    size_t e = line.find('"', q + 1);
    if (e == std::string::npos) continue;
    std::string key = line.substr(q + 1, e - q - 1);
    // A value line ("#Action_Fire") never appears first on a line.
    if (!key.empty() && key[0] != '#') out.insert(key);
  }
  return out;
}

int main() {
  std::ifstream f("steam/game_actions_4536720.vdf");
  CHECK(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  std::string vdf = ss.str();
  // Strip // comment lines so a commented-out action can't pass.
  {
    std::istringstream in(vdf);
    std::string line, kept;
    while (std::getline(in, line)) {
      size_t c = line.find("//");
      if (c != std::string::npos) line = line.substr(0, c);
      kept += line + "\n";
    }
    vdf = kept;
  }

  std::string ship = set_block(vdf, pad_action_set_name(PAD_SET_SHIP));
  std::string menu = set_block(vdf, pad_action_set_name(PAD_SET_MENU));
  CHECK(!ship.empty());
  CHECK(!menu.empty());
  CHECK(has_key(vdf, "Set_Ship") && has_key(vdf, "Set_Menu"));

  std::set<std::string> declared;
  for (int s = 0; s < PAD_SET_COUNT; s++) {
    const std::string &blk = s == PAD_SET_SHIP ? ship : menu;
    std::set<std::string> b = button_keys(kind_block(blk, "Button"));
    std::set<std::string> a = button_keys(kind_block(blk, "StickPadGyro"));
    for (std::set<std::string>::iterator it = b.begin(); it != b.end(); ++it)
      declared.insert(*it);
    for (std::set<std::string>::iterator it = a.begin(); it != a.end(); ++it)
      if (*it != "title" && *it != "input_mode") declared.insert(*it);
  }

  std::set<std::string> names;
  for (int i = 0; i < PAD_ACT_COUNT; i++) {
    PadAction a = (PadAction)i;
    const PadActionInfo &info = pad_action_info(a);
    CHECKF(info.name && *info.name, "action %d has no name", i);
    CHECKF(names.insert(info.name).second, "duplicate action name %s", info.name);
    CHECK(info.set == PAD_SET_SHIP || info.set == PAD_SET_MENU);
    const std::string &blk = info.set == PAD_SET_SHIP ? ship : menu;
    std::string kind = kind_block(blk, info.analog ? "StickPadGyro" : "Button");
    CHECKF(has_key(kind, info.name), "%s missing from the %s set's %s block",
           info.name, pad_action_set_name(info.set), info.analog ? "StickPadGyro" : "Button");
    // ...and not ALSO in the other kind, or the other set.
    std::string other_kind = kind_block(blk, info.analog ? "Button" : "StickPadGyro");
    CHECKF(!has_key(other_kind, info.name), "%s declared under the wrong kind", info.name);
    const std::string &other = info.set == PAD_SET_SHIP ? menu : ship;
    CHECKF(!has_key(other, info.name), "%s declared in both sets", info.name);
    // A localization string for the title.
    CHECKF(info.title && *info.title, "%s has no title", info.name);
    // An analog action rides the left stick's axes; a digital one names a
    // button the consumers can switch on.
    if (info.analog) CHECK(info.button == PAD_BUTTON_LEFT_STICK);
    else CHECK(info.button >= 0 && info.button != PAD_BUTTON_LEFT_STICK &&
               info.button != PAD_BUTTON_RIGHT_STICK &&
               info.button != PAD_BUTTON_LEFT_TRIGGER &&
               info.button != PAD_BUTTON_RIGHT_TRIGGER);
    declared.erase(info.name);
  }
  // Nothing in the manifest the game doesn't read.
  for (std::set<std::string>::iterator it = declared.begin(); it != declared.end(); ++it)
    CHECKF(false, "manifest declares %s, which pad.h does not know", it->c_str());

  // Distinct synthesized buttons per set.
  for (int s = 0; s < PAD_SET_COUNT; s++) {
    std::set<int> used;
    for (int i = 0; i < PAD_ACT_COUNT; i++) {
      const PadActionInfo &info = pad_action_info((PadAction)i);
      if (info.set != s || info.analog) continue;
      CHECKF(used.insert(info.button).second, "%s shares its SDL button with another %s action",
             info.name, pad_action_set_name((PadActionSet)s));
    }
  }

  // The positions the consumers hard-code (GLShip::controller_input,
  // State::nav_key_from_controller, GLGame::controller, the lobby).
  CHECK(pad_action_info(PAD_ACT_FIRE).button == SDL_CONTROLLER_BUTTON_A);
  CHECK(pad_action_info(PAD_ACT_FIRE).trigger_axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
  CHECK(pad_action_info(PAD_ACT_SECONDARY).button == SDL_CONTROLLER_BUTTON_B);
  CHECK(pad_action_info(PAD_ACT_NEXT_WEAPON).button == SDL_CONTROLLER_BUTTON_X);
  CHECK(pad_action_info(PAD_ACT_NEXT_SECONDARY).button == SDL_CONTROLLER_BUTTON_Y);
  CHECK(pad_action_info(PAD_ACT_BOOST).button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
  CHECK(pad_action_info(PAD_ACT_TELEPORT).button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  CHECK(pad_action_info(PAD_ACT_ROTATE_VIEW).button == SDL_CONTROLLER_BUTTON_LEFTSTICK);
  CHECK(pad_action_info(PAD_ACT_HELP).button == SDL_CONTROLLER_BUTTON_RIGHTSTICK);
  CHECK(pad_action_info(PAD_ACT_PAUSE).button == SDL_CONTROLLER_BUTTON_START);
  CHECK(pad_action_info(PAD_ACT_MENU).button == SDL_CONTROLLER_BUTTON_BACK);
  CHECK(pad_action_info(PAD_ACT_THRUST).button == SDL_CONTROLLER_BUTTON_DPAD_UP);
  CHECK(pad_action_info(PAD_ACT_CONFIRM).button == SDL_CONTROLLER_BUTTON_A);
  CHECK(pad_action_info(PAD_ACT_BACK).button == SDL_CONTROLLER_BUTTON_B);
  CHECK(pad_action_info(PAD_ACT_START).button == SDL_CONTROLLER_BUTTON_START);
  CHECK(pad_action_info(PAD_ACT_EXIT).button == SDL_CONTROLLER_BUTTON_BACK);
  CHECK(pad_action_info(PAD_ACT_PASTE).button == SDL_CONTROLLER_BUTTON_X);
  CHECK(pad_action_info(PAD_ACT_KEYBOARD).button == SDL_CONTROLLER_BUTTON_Y);
  CHECK(pad_action_info(PAD_ACT_NAV_UP).button == SDL_CONTROLLER_BUTTON_DPAD_UP);
  // The zoom actions live on positions no other action uses and the
  // pseudo-button labels read as words (never circled).
  CHECK(strlen(pad_button_label(PAD_STYLE_XBOX, PAD_BUTTON_ZOOM_IN)) > 1);
  CHECK(strlen(pad_button_label(PAD_STYLE_PS5, PAD_BUTTON_RIGHT_TRIGGER)) > 1);
  CHECK(strcmp(pad_button_label(PAD_STYLE_PS5, PAD_BUTTON_RIGHT_TRIGGER), "R2") == 0);
  CHECK(strcmp(pad_button_label(PAD_STYLE_XBOX, PAD_BUTTON_RIGHT_TRIGGER), "RT") == 0);
  CHECK(strcmp(pad_button_label(PAD_STYLE_XBOX, PAD_BUTTON_LEFT_STICK), "LEFT STICK") == 0);
  // Steam ids never collide with SDL instance ids.
  CHECK(!pad_is_steam(0) && !pad_is_steam(PAD_NONE) && pad_is_steam(PAD_STEAM_BASE));

  // The generated action manifest (steam/make_input_manifest.py) carries
  // the same action sets: a stale manifest ships bindings for actions the
  // game no longer reads.
  {
    std::ifstream mf("steam/steam_input_manifest.vdf");
    CHECK(mf.good());
    std::stringstream ms;
    ms << mf.rdbuf();
    std::string manifest = ms.str();
    CHECK(manifest.find("\"Action Manifest\"") == 0);
    CHECK(has_key(manifest, "configurations"));
    // Valve's documented shape: configurations keyed by controller type,
    // then priority, each entry a "path" — the inverse (numeric keys with
    // a "controller_type" field) parsed and yielded no defaults (2026-09-07).
    CHECK(!has_key(manifest, "controller_type"));
    {
      std::string cfg = kind_block(manifest, "configurations");
      CHECK(!cfg.empty());
      std::set<std::string> types = button_keys(cfg);
      CHECKF(types.count("controller_xboxone") == 1, "configurations lacks controller_xboxone");
      for (std::set<std::string>::iterator it = types.begin(); it != types.end(); ++it) {
        if (*it == "0" || *it == "path") continue;
        CHECKF(it->compare(0, 11, "controller_") == 0, "configurations key %s is not a controller type", it->c_str());
        std::string entry = kind_block(cfg, it->c_str());
        CHECKF(has_key(entry, "0") && has_key(entry, "path"), "%s entry lacks \"0\" { \"path\" }", it->c_str());
      }
    }
    for (int i = 0; i < PAD_ACT_COUNT; i++)
      CHECKF(has_key(manifest, pad_action_info((PadAction)i).name),
             "manifest lacks %s — re-run steam/make_input_manifest.py", pad_action_info((PadAction)i).name);
  }

  printf("pad_actions_test: %s\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
