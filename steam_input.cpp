// Steam Input API pad backend — see steam_input.h and STEAMINPUT.md.
// Compiled to nothing outside STEAM_BUILD (the header's inline stubs
// stand in).

#ifdef STEAM_BUILD

#include "steam_input.h"
#include "startup_trace.h"
#include "state_manager.h"

#include <SDL.h>
#include <steam/steam_api.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// The depot carries three Steam Input files beside the binary
// (SDL_GetBasePath: the executable's directory on Linux/Windows,
// Contents/Resources inside the macOS bundle; deploy-steam.yml and `make
// steam` stage them there): the In-Game Actions file (the action sets),
// the exported default layouts (controller_<type>.vdf), and the ACTION
// MANIFEST — the actions plus a "configurations" block naming those
// layouts (steam/make_input_manifest.py generates it). The manifest is
// what the portal's Steam Input page is told about and what
// SetInputActionManifestFilePath takes — the call REFUSED the actions
// file itself (field, 2026-09-06). Handing Steam the path here is belt
// and braces beside the portal setting: a local build under
// steam_run_local.sh gets its layouts without the portal or a
// controller_config/ copy.
const char *ACTIONS_NAME = "game_actions_4536720.vdf";
const char *MANIFEST_NAME = "steam_input_manifest.vdf";

struct SteamPad {
  InputHandle_t handle;
  PadId id;                   // PAD_NONE until adopted
  ESteamInputType type;
  bool adopted;               // its layout uses the game's actions (see steam_input.h)
  bool legacy_traced;         // the "left to SDL" line, once per handle
  int inactive_ticks;         // adopted but every action inactive, consecutive
  bool set_known;
  PadActionSet set;
  bool held[PAD_ACT_COUNT];   // last state sent for each digital action
  // A digital action fires only after it has been SEEN RELEASED since its
  // set was activated. The same physical button is `pause` in the Ship
  // set and `start` in the Menu set, and a pause switches the set — so a
  // Start still held across the switch read as a fresh press in the new
  // set, which toggled the pause back, which switched the set again:
  // Start oscillated the pause for as long as it was held (field,
  // 2026-09-06). A press belongs to the set it began in; the next set
  // needs its own press.
  bool primed[PAD_ACT_COUNT];
  // Some action in some set has reported an origin: the layout's
  // bindings are loaded. Until then every action reads unbound, which is
  // "not yet", not "unbound".
  bool bindings_seen;
  int bindings_poll;          // ticks until the next check while unseen
  Sint16 axis_x, axis_y;      // last emitted stick values
};
// An adopted pad whose layout stops using the actions (the player picked a
// gamepad template in the overlay) is handed back to SDL after this many
// consecutive all-inactive ticks — long enough to ride out a layout
// reload, short enough to feel like a switch.
const int INACTIVE_DROP_TICKS = 60;

bool g_active = false;
std::vector<SteamPad> g_pads;
PadId g_next_id = PAD_STEAM_BASE;
InputActionSetHandle_t g_set[PAD_SET_COUNT] = {0, 0};
InputDigitalActionHandle_t g_digital[PAD_ACT_COUNT] = {};
InputAnalogActionHandle_t g_analog[PAD_ACT_COUNT] = {};

SteamPad *find_pad(PadId id) {
  if (id == PAD_NONE) return NULL;
  for (size_t i = 0; i < g_pads.size(); i++)
    if (g_pads[i].id == id && g_pads[i].adopted) return &g_pads[i];
  return NULL;
}

int adopted_count() {
  int n = 0;
  for (size_t i = 0; i < g_pads.size(); i++)
    if (g_pads[i].adopted) n++;
  return n;
}

const char *type_name(ESteamInputType t) {
  switch (t) {
    case k_ESteamInputType_SteamController:     return "Steam Controller";
    case k_ESteamInputType_XBox360Controller:   return "Xbox 360 Controller";
    case k_ESteamInputType_XBoxOneController:   return "Xbox One Controller";
    case k_ESteamInputType_GenericGamepad:      return "Generic Gamepad";
    case k_ESteamInputType_PS4Controller:       return "PS4 Controller";
    case k_ESteamInputType_PS3Controller:       return "PS3 Controller";
    case k_ESteamInputType_PS5Controller:       return "PS5 Controller";
    case k_ESteamInputType_SwitchProController: return "Switch Pro Controller";
    case k_ESteamInputType_SwitchJoyConPair:    return "Joy-Con Pair";
    case k_ESteamInputType_SwitchJoyConSingle:  return "Joy-Con";
    case k_ESteamInputType_SteamDeckController: return "Steam Deck";
    case k_ESteamInputType_MobileTouch:         return "Steam Link Touch";
    default:                                    return "Controller";
  }
}

// ---- synthetic events ----
// The consumers speak SDL_Event; give them exactly that, with the pad's
// PadId in `which`. Zeroed first: cbutton/caxis share the union with
// larger event bodies and a stale padding byte is a stale field somewhere.
void emit_button(StateManager *game, PadId id, int button, bool down) {
  SDL_Event e;
  SDL_zero(e);
  e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
  e.cbutton.type = e.type;
  e.cbutton.timestamp = SDL_GetTicks();
  e.cbutton.which = id;
  e.cbutton.button = (Uint8)button;
  e.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
  game->controller(e);
}

void emit_axis(StateManager *game, PadId id, int axis, Sint16 value) {
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_CONTROLLERAXISMOTION;
  e.caxis.type = e.type;
  e.caxis.timestamp = SDL_GetTicks();
  e.caxis.which = id;
  e.caxis.axis = (Uint8)axis;
  e.caxis.value = value;
  game->controller(e);
}

// Everything the pad is holding goes UP: on a set switch (the outgoing
// set's fire must not stay latched into the pause menu, nor a menu confirm
// into play) and on disconnect (the seat's controller_lost releases its
// ship, but the nav latches and the join ladder only ever see events).
void release_all(StateManager *game, SteamPad &p) {
  for (int a = 0; a < PAD_ACT_COUNT; a++) {
    if (!p.held[a]) continue;
    p.held[a] = false;
    emit_button(game, p.id, pad_action_info((PadAction)a).button, false);
  }
  if (p.axis_x != 0) { p.axis_x = 0; emit_axis(game, p.id, SDL_CONTROLLER_AXIS_LEFTX, 0); }
  if (p.axis_y != 0) { p.axis_y = 0; emit_axis(game, p.id, SDL_CONTROLLER_AXIS_LEFTY, 0); }
}

Sint16 to_sdl(float v) {
  if (v > 1.0f) v = 1.0f;
  if (v < -1.0f) v = -1.0f;
  return (Sint16)(v * 32767.0f);
}

// Axis events only on a change worth a step (the consumers' thresholds
// are 8000/10000/16000 counts, so 64 is well inside their resolution) or
// on the return to exactly zero, which the latches need to see.
bool axis_changed(Sint16 was, Sint16 now) {
  if (now == was) return false;
  if (now == 0) return true;
  int d = (int)now - (int)was;
  return d >= 64 || d <= -64;
}

// The origin -> position table (STEAMINPUT.md §4): the families Steam
// reports for the pads we care about, face buttons, bumpers, triggers,
// stick clicks and moves, d-pad, Start/Back equivalents. Nintendo pads map
// by PRINTED letter (Switch_A is the pad's A, whatever its position),
// since a label is what the player reads off the plastic. Anything not
// here — back grips, trackpads, gyro, the Deck's L4/L5 — falls to Steam's
// own string for it.
int origin_button(EInputActionOrigin o) {
  switch (o) {
    case k_EInputActionOrigin_SteamController_A:
    case k_EInputActionOrigin_PS4_X:
    case k_EInputActionOrigin_PS5_X:
    case k_EInputActionOrigin_XBoxOne_A:
    case k_EInputActionOrigin_XBox360_A:
    case k_EInputActionOrigin_Switch_A:
    case k_EInputActionOrigin_SteamDeck_A:
      return SDL_CONTROLLER_BUTTON_A;
    case k_EInputActionOrigin_SteamController_B:
    case k_EInputActionOrigin_PS4_Circle:
    case k_EInputActionOrigin_PS5_Circle:
    case k_EInputActionOrigin_XBoxOne_B:
    case k_EInputActionOrigin_XBox360_B:
    case k_EInputActionOrigin_Switch_B:
    case k_EInputActionOrigin_SteamDeck_B:
      return SDL_CONTROLLER_BUTTON_B;
    case k_EInputActionOrigin_SteamController_X:
    case k_EInputActionOrigin_PS4_Square:
    case k_EInputActionOrigin_PS5_Square:
    case k_EInputActionOrigin_XBoxOne_X:
    case k_EInputActionOrigin_XBox360_X:
    case k_EInputActionOrigin_Switch_X:
    case k_EInputActionOrigin_SteamDeck_X:
      return SDL_CONTROLLER_BUTTON_X;
    case k_EInputActionOrigin_SteamController_Y:
    case k_EInputActionOrigin_PS4_Triangle:
    case k_EInputActionOrigin_PS5_Triangle:
    case k_EInputActionOrigin_XBoxOne_Y:
    case k_EInputActionOrigin_XBox360_Y:
    case k_EInputActionOrigin_Switch_Y:
    case k_EInputActionOrigin_SteamDeck_Y:
      return SDL_CONTROLLER_BUTTON_Y;
    case k_EInputActionOrigin_SteamController_LeftBumper:
    case k_EInputActionOrigin_PS4_LeftBumper:
    case k_EInputActionOrigin_PS5_LeftBumper:
    case k_EInputActionOrigin_XBoxOne_LeftBumper:
    case k_EInputActionOrigin_XBox360_LeftBumper:
    case k_EInputActionOrigin_Switch_LeftBumper:
    case k_EInputActionOrigin_SteamDeck_L1:
      return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    case k_EInputActionOrigin_SteamController_RightBumper:
    case k_EInputActionOrigin_PS4_RightBumper:
    case k_EInputActionOrigin_PS5_RightBumper:
    case k_EInputActionOrigin_XBoxOne_RightBumper:
    case k_EInputActionOrigin_XBox360_RightBumper:
    case k_EInputActionOrigin_Switch_RightBumper:
    case k_EInputActionOrigin_SteamDeck_R1:
      return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    case k_EInputActionOrigin_SteamController_Start:
    case k_EInputActionOrigin_PS4_Options:
    case k_EInputActionOrigin_PS5_Option:
    case k_EInputActionOrigin_XBoxOne_Menu:
    case k_EInputActionOrigin_XBox360_Start:
    case k_EInputActionOrigin_Switch_Plus:
    case k_EInputActionOrigin_SteamDeck_Menu:
      return SDL_CONTROLLER_BUTTON_START;
    case k_EInputActionOrigin_SteamController_Back:
    case k_EInputActionOrigin_PS4_Share:
    case k_EInputActionOrigin_PS5_Create:
    case k_EInputActionOrigin_XBoxOne_View:
    case k_EInputActionOrigin_XBox360_Back:
    case k_EInputActionOrigin_Switch_Minus:
    case k_EInputActionOrigin_SteamDeck_View:
      return SDL_CONTROLLER_BUTTON_BACK;
    case k_EInputActionOrigin_SteamController_LeftTrigger_Pull:
    case k_EInputActionOrigin_SteamController_LeftTrigger_Click:
    case k_EInputActionOrigin_PS4_LeftTrigger_Pull:
    case k_EInputActionOrigin_PS4_LeftTrigger_Click:
    case k_EInputActionOrigin_PS5_LeftTrigger_Pull:
    case k_EInputActionOrigin_PS5_LeftTrigger_Click:
    case k_EInputActionOrigin_XBoxOne_LeftTrigger_Pull:
    case k_EInputActionOrigin_XBoxOne_LeftTrigger_Click:
    case k_EInputActionOrigin_XBox360_LeftTrigger_Pull:
    case k_EInputActionOrigin_XBox360_LeftTrigger_Click:
    case k_EInputActionOrigin_Switch_LeftTrigger_Pull:
    case k_EInputActionOrigin_Switch_LeftTrigger_Click:
    case k_EInputActionOrigin_SteamDeck_L2_SoftPull:
    case k_EInputActionOrigin_SteamDeck_L2:
      return PAD_BUTTON_LEFT_TRIGGER;
    case k_EInputActionOrigin_SteamController_RightTrigger_Pull:
    case k_EInputActionOrigin_SteamController_RightTrigger_Click:
    case k_EInputActionOrigin_PS4_RightTrigger_Pull:
    case k_EInputActionOrigin_PS4_RightTrigger_Click:
    case k_EInputActionOrigin_PS5_RightTrigger_Pull:
    case k_EInputActionOrigin_PS5_RightTrigger_Click:
    case k_EInputActionOrigin_XBoxOne_RightTrigger_Pull:
    case k_EInputActionOrigin_XBoxOne_RightTrigger_Click:
    case k_EInputActionOrigin_XBox360_RightTrigger_Pull:
    case k_EInputActionOrigin_XBox360_RightTrigger_Click:
    case k_EInputActionOrigin_Switch_RightTrigger_Pull:
    case k_EInputActionOrigin_Switch_RightTrigger_Click:
    case k_EInputActionOrigin_SteamDeck_R2_SoftPull:
    case k_EInputActionOrigin_SteamDeck_R2:
      return PAD_BUTTON_RIGHT_TRIGGER;
    case k_EInputActionOrigin_SteamController_LeftStick_Click:
    case k_EInputActionOrigin_PS4_LeftStick_Click:
    case k_EInputActionOrigin_PS5_LeftStick_Click:
    case k_EInputActionOrigin_XBoxOne_LeftStick_Click:
    case k_EInputActionOrigin_XBox360_LeftStick_Click:
    case k_EInputActionOrigin_Switch_LeftStick_Click:
    case k_EInputActionOrigin_SteamDeck_L3:
      return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    case k_EInputActionOrigin_PS4_RightStick_Click:
    case k_EInputActionOrigin_PS5_RightStick_Click:
    case k_EInputActionOrigin_XBoxOne_RightStick_Click:
    case k_EInputActionOrigin_XBox360_RightStick_Click:
    case k_EInputActionOrigin_Switch_RightStick_Click:
    case k_EInputActionOrigin_SteamDeck_R3:
      return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    case k_EInputActionOrigin_SteamController_LeftStick_Move:
    case k_EInputActionOrigin_PS4_LeftStick_Move:
    case k_EInputActionOrigin_PS5_LeftStick_Move:
    case k_EInputActionOrigin_XBoxOne_LeftStick_Move:
    case k_EInputActionOrigin_XBox360_LeftStick_Move:
    case k_EInputActionOrigin_Switch_LeftStick_Move:
    case k_EInputActionOrigin_SteamDeck_LeftStick_Move:
      return PAD_BUTTON_LEFT_STICK;
    case k_EInputActionOrigin_PS4_RightStick_Move:
    case k_EInputActionOrigin_PS5_RightStick_Move:
    case k_EInputActionOrigin_XBoxOne_RightStick_Move:
    case k_EInputActionOrigin_XBox360_RightStick_Move:
    case k_EInputActionOrigin_Switch_RightStick_Move:
    case k_EInputActionOrigin_SteamDeck_RightStick_Move:
      return PAD_BUTTON_RIGHT_STICK;
    case k_EInputActionOrigin_PS4_DPad_North:
    case k_EInputActionOrigin_PS5_DPad_North:
    case k_EInputActionOrigin_XBoxOne_DPad_North:
    case k_EInputActionOrigin_XBox360_DPad_North:
    case k_EInputActionOrigin_Switch_DPad_North:
    case k_EInputActionOrigin_SteamDeck_DPad_North:
      return SDL_CONTROLLER_BUTTON_DPAD_UP;
    case k_EInputActionOrigin_PS4_DPad_South:
    case k_EInputActionOrigin_PS5_DPad_South:
    case k_EInputActionOrigin_XBoxOne_DPad_South:
    case k_EInputActionOrigin_XBox360_DPad_South:
    case k_EInputActionOrigin_Switch_DPad_South:
    case k_EInputActionOrigin_SteamDeck_DPad_South:
      return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    case k_EInputActionOrigin_PS4_DPad_West:
    case k_EInputActionOrigin_PS5_DPad_West:
    case k_EInputActionOrigin_XBoxOne_DPad_West:
    case k_EInputActionOrigin_XBox360_DPad_West:
    case k_EInputActionOrigin_Switch_DPad_West:
    case k_EInputActionOrigin_SteamDeck_DPad_West:
      return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    case k_EInputActionOrigin_PS4_DPad_East:
    case k_EInputActionOrigin_PS5_DPad_East:
    case k_EInputActionOrigin_XBoxOne_DPad_East:
    case k_EInputActionOrigin_XBox360_DPad_East:
    case k_EInputActionOrigin_Switch_DPad_East:
    case k_EInputActionOrigin_SteamDeck_DPad_East:
      return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    default:
      return -1;
  }
}

// Steam's own name for an origin, upper-cased into the Typer face (the
// font is caps; "Left Grip" would render as its own caps anyway, but a
// stray lowercase glyph is a missing glyph).
std::string &origin_text(EInputActionOrigin o) {
  static std::string s;
  const char *t = SteamInput() ? SteamInput()->GetStringForActionOrigin(o) : NULL;
  s = t ? t : "";
  for (size_t i = 0; i < s.size(); i++)
    if (s[i] >= 'a' && s[i] <= 'z') s[i] = (char)(s[i] - 'a' + 'A');
  return s;
}

// What the layout binds in a set, action by action: origin count (0 =
// unbound, which the HUD renders as "-") and whether the action is
// active (in the layout at all). Traced on adoption and on every set
// switch — the field question it answers is "a pad that plays but whose
// hints read '-': is the set unbound, or is the origin query failing?"
void trace_set(ISteamInput *in, const SteamPad &p, PadActionSet set) {
  for (int a = 0; a < PAD_ACT_COUNT; a++) {
    const PadActionInfo &info = pad_action_info((PadAction)a);
    if (info.set != set) continue;
    EInputActionOrigin origins[STEAM_INPUT_MAX_ORIGINS];
    int n = info.analog
        ? in->GetAnalogActionOrigins(p.handle, g_set[set], g_analog[a], origins)
        : in->GetDigitalActionOrigins(p.handle, g_set[set], g_digital[a], origins);
    bool active = info.analog ? in->GetAnalogActionData(p.handle, g_analog[a]).bActive
                              : in->GetDigitalActionData(p.handle, g_digital[a]).bActive;
    startup_tracef("  %-15s handle=%llu origins=%d active=%d%s%s", info.name,
                   (unsigned long long)(info.analog ? g_analog[a] : g_digital[a]), n,
                   (int)active, n > 0 ? " first=" : "",
                   n > 0 ? origin_text(origins[0]).c_str() : "");
  }
}

// Does the layout use the game's actions in this set at all? bActive is
// Steam's "this action exists in the active configuration"; all-false is
// a legacy gamepad template (XInput emulation) — SDL's pad, not ours.
bool any_action_active(ISteamInput *in, const SteamPad &p, PadActionSet set) {
  for (int a = 0; a < PAD_ACT_COUNT; a++) {
    const PadActionInfo &info = pad_action_info((PadAction)a);
    if (info.set != set) continue;
    bool active = info.analog ? in->GetAnalogActionData(p.handle, g_analog[a]).bActive
                              : in->GetDigitalActionData(p.handle, g_digital[a]).bActive;
    if (active) return true;
  }
  return false;
}

// Keep the pad's active set in step with the screen. Every handle gets
// this, adopted or not: bActive is only meaningful for the ACTIVE set, so
// an un-adopted handle must sit in the wanted set for the probe to see a
// layout that binds it.
void sync_set(StateManager *game, SteamPad &p, PadActionSet want) {
  ISteamInput *in = SteamInput();
  if (p.set_known && p.set == want) return;
  if (p.adopted) release_all(game, p);
  in->ActivateActionSet(p.handle, g_set[want]);
  p.set = want;
  p.set_known = true;
  for (int a = 0; a < PAD_ACT_COUNT; a++) p.primed[a] = false;
  if (p.adopted) {
    startup_tracef("steam input: pad %d -> action set %s", p.id - PAD_STEAM_BASE,
                   pad_action_set_name(want));
    trace_set(in, p, want);
  }
}

// Have the layout's bindings reached this pad yet? Any origin on any
// action of the active set says yes; checked every ~half second until so.
void check_bindings(ISteamInput *in, SteamPad &p, PadActionSet set) {
  if (p.bindings_seen) return;
  if (p.bindings_poll-- > 0) return;
  p.bindings_poll = 30;
  for (int a = 0; a < PAD_ACT_COUNT; a++) {
    const PadActionInfo &info = pad_action_info((PadAction)a);
    if (info.set != set) continue;
    EInputActionOrigin origins[STEAM_INPUT_MAX_ORIGINS];
    int n = info.analog
        ? in->GetAnalogActionOrigins(p.handle, g_set[set], g_analog[a], origins)
        : in->GetDigitalActionOrigins(p.handle, g_set[set], g_digital[a], origins);
    if (n > 0) {
      p.bindings_seen = true;
      startup_tracef("steam input: pad %d bindings loaded", p.id - PAD_STEAM_BASE);
      trace_set(in, p, set);
      return;
    }
  }
}

// Steam's analog joystick_move data is up-positive (XInput's frame — the
// API grew out of the gamepad it emulates); SDL's LEFTY is down-positive.
// Negate on the way through so the ship handlers see what they always saw.
void poll_pad(StateManager *game, SteamPad &p, PadActionSet want) {
  ISteamInput *in = SteamInput();
  check_bindings(in, p, want);
  for (int a = 0; a < PAD_ACT_COUNT; a++) {
    const PadActionInfo &info = pad_action_info((PadAction)a);
    if (info.set != want) continue;
    if (info.analog) {
      InputAnalogActionData_t d = in->GetAnalogActionData(p.handle, g_analog[a]);
      Sint16 x = d.bActive ? to_sdl(d.x) : 0;
      Sint16 y = d.bActive ? to_sdl(-d.y) : 0;
      if (axis_changed(p.axis_x, x)) { p.axis_x = x; emit_axis(game, p.id, SDL_CONTROLLER_AXIS_LEFTX, x); }
      if (axis_changed(p.axis_y, y)) { p.axis_y = y; emit_axis(game, p.id, SDL_CONTROLLER_AXIS_LEFTY, y); }
      continue;
    }
    InputDigitalActionData_t d = in->GetDigitalActionData(p.handle, g_digital[a]);
    // Not in the active set yet (the switch lands a frame late): nothing
    // to read, and not yet a basis for priming either.
    if (!d.bActive) continue;
    bool down = d.bState;
    if (!p.primed[a]) {
      if (!down) p.primed[a] = true;  // released once: live from here
      continue;
    }
    if (down == p.held[a]) continue;
    p.held[a] = down;
    emit_button(game, p.id, info.button, down);
  }
}

}  // namespace

bool steam_input_init() {
  g_active = false;
  const char *off = std::getenv("NEWTONIA_STEAM_INPUT");
  if (off && off[0] == '0' && off[1] == '\0') {
    startup_trace("steam input: disabled by NEWTONIA_STEAM_INPUT=0 (SDL pads)");
    return false;
  }
  ISteamInput *in = SteamInput();
  if (!in) {
    startup_trace("steam input: no ISteamInput (SDL pads)");
    return false;
  }
  char *base = SDL_GetBasePath();
  if (base) {
    std::string dir(base);
    SDL_free(base);
    std::string actions = dir + ACTIONS_NAME;
    SDL_RWops *f = SDL_RWFromFile(actions.c_str(), "rb");
    if (f) SDL_RWclose(f);
    startup_tracef("steam input: IGA %s beside the binary at %s", f ? "present" : "ABSENT", actions.c_str());
    std::string manifest = dir + MANIFEST_NAME;
    f = SDL_RWFromFile(manifest.c_str(), "rb");
    if (f) {
      SDL_RWclose(f);
      bool ok = in->SetInputActionManifestFilePath(manifest.c_str());
      startup_tracef("steam input: action manifest %s (%s)", manifest.c_str(), ok ? "set" : "REFUSED");
    } else {
      startup_tracef("steam input: no action manifest at %s (portal / controller_config govern)", manifest.c_str());
    }
  }
  // Explicit RunFrame: the poll paces its own reads, right before the
  // game tick, instead of riding the callback pump.
  if (!in->Init(true)) {
    startup_trace("steam input: Init FAILED (SDL pads)");
    return false;
  }
  in->RunFrame();
  for (int s = 0; s < PAD_SET_COUNT; s++)
    g_set[s] = in->GetActionSetHandle(pad_action_set_name((PadActionSet)s));
  // A 0 handle means Steam has no In-Game Actions for this app — the IGA
  // is neither uploaded in the portal nor in controller_config — so no
  // layout could ever bind anything and every pad would be inert (field,
  // 2026-09-06: "PRESS -" and no controller at all). That is STEAMINPUT.md
  // §5 rule 2's "fallback is total" case in another guise: hand the pads
  // back to SDL, where Steam's gamepad emulation drives them exactly as the
  // shipped build's.
  if (g_set[PAD_SET_SHIP] == 0 || g_set[PAD_SET_MENU] == 0) {
    startup_trace("steam input: action sets UNKNOWN — In-Game Actions file not registered "
                  "(portal Steam Input section, or <Steam>/controller_config/) — SDL pads");
    in->Shutdown();
    return false;
  }
  for (int a = 0; a < PAD_ACT_COUNT; a++) {
    const PadActionInfo &info = pad_action_info((PadAction)a);
    if (info.analog) g_analog[a] = in->GetAnalogActionHandle(info.name);
    else g_digital[a] = in->GetDigitalActionHandle(info.name);
  }
  // Handles are one namespace per app: every action distinct. Two
  // definitions of the actions loaded at once (a controller_config/ dev
  // copy beside the bundled manifest, the first launch after the manifest
  // path was set — field, 2026-09-06) handed out colliding handles, and
  // the Menu set then read the Ship set's bindings under the wrong names
  // (start = X, paste = right shoulder). A colliding table is not a pad
  // backend; hand the pads to SDL and say why.
  for (int a = 0; a < PAD_ACT_COUNT; a++) {
    for (int b = a + 1; b < PAD_ACT_COUNT; b++) {
      const PadActionInfo &ia = pad_action_info((PadAction)a);
      const PadActionInfo &ib = pad_action_info((PadAction)b);
      if (ia.analog != ib.analog) continue;
      unsigned long long ha = ia.analog ? g_analog[a] : g_digital[a];
      unsigned long long hb = ib.analog ? g_analog[b] : g_digital[b];
      if (ha != hb) continue;
      startup_tracef("steam input: action handles COLLIDE (%s and %s both %llu) — two definitions "
                     "of the actions loaded? (a controller_config/ copy beside the bundled manifest) — SDL pads",
                     ia.name, ib.name, ha);
      in->Shutdown();
      return false;
    }
  }
  g_active = true;
  startup_tracef("steam input: Init ok, sets Ship=%llu Menu=%llu",
                 (unsigned long long)g_set[PAD_SET_SHIP],
                 (unsigned long long)g_set[PAD_SET_MENU]);
  return true;
}

bool steam_input_active() { return g_active; }

void steam_input_poll(StateManager *game) {
  if (!g_active || !game) return;
  ISteamInput *in = SteamInput();
  if (!in) return;
  in->RunFrame();
  // Hot-plug by diffing the connected list each tick: one mechanism for
  // startup and later, no callback registration to keep in step with the
  // pump, and immune to the snap client's flaky device callbacks (§9).
  InputHandle_t handles[STEAM_INPUT_MAX_COUNT];
  int n = in->GetConnectedControllers(handles);
  static int last_n = -1;
  if (n != last_n) {
    last_n = n;
    startup_tracef("steam input: handles presented=%d", n);
  }
  for (size_t i = 0; i < g_pads.size();) {
    bool still = false;
    for (int k = 0; k < n; k++)
      if (handles[k] == g_pads[i].handle) { still = true; break; }
    if (still) { i++; continue; }
    SteamPad gone = g_pads[i];
    g_pads.erase(g_pads.begin() + i);
    if (!gone.adopted) continue;
    release_all(game, gone);
    startup_tracef("steam input: pad %d disconnected", gone.id - PAD_STEAM_BASE);
    pad_forget(gone.id);
    game->controller_removed(gone.id);
  }
  for (int k = 0; k < n; k++) {
    bool known = false;
    for (size_t i = 0; i < g_pads.size(); i++)
      if (g_pads[i].handle == handles[k]) { known = true; break; }
    if (known) continue;
    SteamPad p;
    memset(&p, 0, sizeof p);
    p.handle = handles[k];
    p.id = PAD_NONE;
    p.type = in->GetInputTypeForHandle(handles[k]);
    g_pads.push_back(p);
    startup_tracef("steam input: handle %llu seen: %s", (unsigned long long)p.handle,
                   type_name(p.type));
  }
  PadActionSet want = game->pad_action_set();
  for (size_t i = 0; i < g_pads.size(); i++) {
    SteamPad &p = g_pads[i];
    sync_set(game, p, want);
    bool active = any_action_active(in, p, want);
    if (!p.adopted) {
      if (!active) {
        // The set activation lands a frame late, so the first tick after
        // a handle appears reads every action inactive whatever the
        // layout — say "legacy template" only once that has held a while.
        if (!p.legacy_traced && ++p.inactive_ticks >= INACTIVE_DROP_TICKS) {
          p.legacy_traced = true;
          startup_tracef("steam input: handle %llu: layout has no %s-set actions "
                         "(legacy gamepad template) — SDL's emulated pad drives it",
                         (unsigned long long)p.handle, pad_action_set_name(want));
        }
        continue;
      }
      // The layout binds the game's actions: this pad is ours from here.
      p.adopted = true;
      p.legacy_traced = false;
      p.inactive_ticks = 0;
      p.bindings_seen = false;
      p.bindings_poll = 0;
      p.id = g_next_id++;
      startup_tracef("steam input: pad %d adopted (handle %llu): %s (%s glyphs)",
                     p.id - PAD_STEAM_BASE, (unsigned long long)p.handle,
                     type_name(p.type), pad_style_name(pad_style_for_id(p.id)));
      trace_set(in, p, want);
      game->controller_added(p.id);
    } else if (!active) {
      if (++p.inactive_ticks < INACTIVE_DROP_TICKS) continue;
      // Back to a gamepad template: SDL's emulated device is the pad now.
      startup_tracef("steam input: pad %d released (handle %llu): layout no longer uses the actions",
                     p.id - PAD_STEAM_BASE, (unsigned long long)p.handle);
      release_all(game, p);
      PadId old = p.id;
      p.adopted = false;
      p.inactive_ticks = 0;
      p.id = PAD_NONE;
      pad_forget(old);
      game->controller_removed(old);
      continue;
    } else {
      p.inactive_ticks = 0;
    }
    poll_pad(game, p, want);
  }
}

int steam_input_handle_count() { return g_active ? (int)g_pads.size() : 0; }

bool steam_input_owns_handle(unsigned long long handle) {
  if (!g_active || handle == 0) return false;
  for (size_t i = 0; i < g_pads.size(); i++)
    if (g_pads[i].handle == handle) return g_pads[i].adopted;
  return false;
}

void steam_input_shutdown() {
  if (!g_active) return;
  g_active = false;
  g_pads.clear();
  if (SteamInput()) SteamInput()->Shutdown();
}

bool steam_input_attached(PadId id) { return find_pad(id) != NULL; }

int steam_input_count() { return adopted_count(); }

PadId steam_input_id_at(int index) {
  int seen = 0;
  for (size_t i = 0; i < g_pads.size(); i++) {
    if (!g_pads[i].adopted) continue;
    if (seen == index) return g_pads[i].id;
    seen++;
  }
  return PAD_NONE;
}

const char *steam_input_name(PadId id) {
  SteamPad *p = find_pad(id);
  return p ? type_name(p->type) : "Steam pad";
}

PadStyle steam_input_style(PadId id) {
  SteamPad *p = find_pad(id);
  if (!p) return PAD_STYLE_XBOX;
  switch (p->type) {
    case k_ESteamInputType_PS4Controller:
    case k_ESteamInputType_PS3Controller: return PAD_STYLE_PS4;
    case k_ESteamInputType_PS5Controller: return PAD_STYLE_PS5;
    default:                              return PAD_STYLE_XBOX;
  }
}

bool steam_input_show_binding_panel(PadId id) {
  SteamPad *p = find_pad(id);
  if (!p || !SteamInput()) return false;
  startup_tracef("steam input: binding panel for pad %d", id - PAD_STEAM_BASE);
  return SteamInput()->ShowBindingPanel(p->handle);
}

int steam_input_action_origin(PadId id, PadAction a, int *button,
                              const char **text) {
  if (button) *button = -1;
  if (text) *text = "";
  SteamPad *p = find_pad(id);
  ISteamInput *in = SteamInput();
  if (!p || !in) return 0;
  const PadActionInfo &info = pad_action_info(a);
  EInputActionOrigin origins[STEAM_INPUT_MAX_ORIGINS];
  int n = info.analog
      ? in->GetAnalogActionOrigins(p->handle, g_set[info.set], g_analog[a], origins)
      : in->GetDigitalActionOrigins(p->handle, g_set[info.set], g_digital[a], origins);
  if (n <= 0) return p->bindings_seen ? 0 : -1;
  if (n > STEAM_INPUT_MAX_ORIGINS) n = STEAM_INPUT_MAX_ORIGINS;
  // The first origin the table knows wins; otherwise the first origin's
  // own name (a grip, a trackpad).
  for (int i = 0; i < n; i++) {
    int b = origin_button(origins[i]);
    if (b >= 0) {
      if (button) *button = b;
      return n;
    }
  }
  if (text) *text = origin_text(origins[0]).c_str();
  return n;
}

#endif  // STEAM_BUILD
