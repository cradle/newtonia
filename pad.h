#ifndef PAD_H
#define PAD_H

// The pad seam (STEAMINPUT.md §1) — everything the game asks about a
// controller, behind one small surface, so the game logic never holds an
// SDL_GameController* and never enumerates SDL devices itself.
//
// A pad is a PadId, an opaque int. Two backends produce them:
//
//   - SDL (every build): the id IS the SDL joystick instance id of a pad
//     the entry point opened (glut.cpp's controllers[] table, xbox_main's
//     s_controllers[], the single pads in android_main/web_main). Small
//     non-negative ints; 0 is valid, PAD_NONE (-1) is "no pad".
//   - Steam Input (STEAM_BUILD, when ISteamInput::Init succeeded):
//     steam_input.cpp allocates ids from PAD_STEAM_BASE upward, one per
//     InputHandle_t, and is then the ONLY source of pad events — SDL's
//     controller subsystem is not even initialised (§5 rule 1). The
//     consumers can't tell: the backend synthesizes the same
//     SDL_CONTROLLERBUTTON*/AXISMOTION events they already speak, with
//     these ids in `which`.
//
// Event delivery stays what it was: StateManager::controller(SDL_Event)
// and controller_added/removed(PadId). This header covers the QUERIES —
// attached, name, count/enumeration, style, an axis read — and the glyph
// lookup that names the position an ACTION sits on (§4), which is where
// the two backends differ most: an SDL pad's positions are the ones the
// game hard-codes (the table below); a Steam pad's are whatever the
// player's current layout binds.

#include "pad_style.h"

typedef int PadId;
static const PadId PAD_NONE = -1;
// Steam Input ids start here; SDL instance ids never reach it.
static const PadId PAD_STEAM_BASE = 1 << 20;
inline bool pad_is_steam(PadId id) { return id >= PAD_STEAM_BASE; }

// ---- The action vocabulary ------------------------------------------
// One entry per In-Game Action in steam/game_actions_4536720.vdf, in two
// sets. The SDL column is the "today's Xbox binding" of STEAMINPUT.md §2
// read both ways: the Steam backend synthesizes THAT SDL event when the
// action fires, and the SDL path labels the action with THAT position.
// Pure and header-inline so test/unit/pad_actions_test.cpp can pin the
// table against the manifest with nothing linked.

enum PadActionSet { PAD_SET_SHIP = 0, PAD_SET_MENU = 1, PAD_SET_COUNT = 2 };

enum PadAction {
  // Ship set (in play, and the Intro screen)
  PAD_ACT_STEER = 0,   // analog: left stick -> LEFTX/LEFTY
  PAD_ACT_THRUST, PAD_ACT_REVERSE, PAD_ACT_TURN_LEFT, PAD_ACT_TURN_RIGHT,
  PAD_ACT_FIRE, PAD_ACT_SECONDARY,
  PAD_ACT_NEXT_WEAPON, PAD_ACT_NEXT_SECONDARY,
  PAD_ACT_BOOST, PAD_ACT_TELEPORT, PAD_ACT_ROTATE_VIEW, PAD_ACT_HELP,
  PAD_ACT_PAUSE,       // Start: pause, and "press START to join" for an unseated pad
  PAD_ACT_MENU,        // Back: the online-safe pause / offline exit
  PAD_ACT_ZOOM_IN, PAD_ACT_ZOOM_OUT,
  // Menu set (main menu, options, stats, pause menu, roster, lobby, replays,
  // the game-over and disconnect cards)
  PAD_ACT_NAV,         // analog: left stick, through the nav hysteresis
  PAD_ACT_NAV_UP, PAD_ACT_NAV_DOWN, PAD_ACT_NAV_LEFT, PAD_ACT_NAV_RIGHT,
  PAD_ACT_CONFIRM,     // A (+ right trigger in the default layout)
  PAD_ACT_BACK,        // B: back one level; delete in the lobby's code entry
  PAD_ACT_START,       // Start: attract dismiss, pause toggle, join
  PAD_ACT_EXIT,        // Back button: exit to menu from the pause screen
  PAD_ACT_PASTE, PAD_ACT_KEYBOARD,   // X / Y in the lobby
  PAD_ACT_COUNT
};

struct PadActionInfo {
  const char *name;     // the manifest key
  PadActionSet set;
  bool analog;          // joystick_move action -> LEFTX/LEFTY
  int button;           // SDL_GameControllerButton / PadPseudoButton the
                        // action synthesizes and the SDL path labels
  int trigger_axis;     // SDL axis that ALSO fires it in the default
                        // layout (-1: none) — a label hint only
  const char *title;    // the configurator's text (#Action_* localization)
};

inline const PadActionInfo &pad_action_info(PadAction a) {
  static const PadActionInfo table[PAD_ACT_COUNT] = {
    { "steer",          PAD_SET_SHIP, true,  PAD_BUTTON_LEFT_STICK,                -1, "Steer" },
    { "thrust",         PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_DPAD_UP,        -1, "Thrust" },
    { "reverse",        PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_DPAD_DOWN,      -1, "Reverse" },
    { "turn_left",      PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_DPAD_LEFT,      -1, "Turn Left" },
    { "turn_right",     PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,     -1, "Turn Right" },
    { "fire",           PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_A,              SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "Fire" },
    { "secondary",      PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_B,              SDL_CONTROLLER_AXIS_TRIGGERLEFT,  "Fire Secondary" },
    { "next_weapon",    PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_X,              -1, "Next Weapon" },
    { "next_secondary", PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_Y,              -1, "Next Secondary" },
    { "boost",          PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,   -1, "Boost" },
    { "teleport",       PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,  -1, "Teleport" },
    { "rotate_view",    PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_LEFTSTICK,      -1, "Rotate View" },
    { "help",           PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_RIGHTSTICK,     -1, "Controls Card" },
    { "pause",          PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_START,          -1, "Pause / Join" },
    { "menu",           PAD_SET_SHIP, false, SDL_CONTROLLER_BUTTON_BACK,           -1, "Menu" },
    { "zoom_in",        PAD_SET_SHIP, false, PAD_BUTTON_ZOOM_IN,                   -1, "Zoom In" },
    { "zoom_out",       PAD_SET_SHIP, false, PAD_BUTTON_ZOOM_OUT,                  -1, "Zoom Out" },
    { "nav",            PAD_SET_MENU, true,  PAD_BUTTON_LEFT_STICK,                -1, "Navigate" },
    { "nav_up",         PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_DPAD_UP,        -1, "Up" },
    { "nav_down",       PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_DPAD_DOWN,      -1, "Down" },
    { "nav_left",       PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_DPAD_LEFT,      -1, "Left" },
    { "nav_right",      PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,     -1, "Right" },
    { "confirm",        PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_A,              SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "Confirm" },
    { "back",           PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_B,              -1, "Back" },
    { "start",          PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_START,          -1, "Start / Resume" },
    { "exit",           PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_BACK,           -1, "Exit To Menu" },
    { "paste",          PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_X,              -1, "Paste Code" },
    { "keyboard",       PAD_SET_MENU, false, SDL_CONTROLLER_BUTTON_Y,              -1, "Keyboard" },
  };
  return table[a];
}

inline const char *pad_action_set_name(PadActionSet s) {
  return s == PAD_SET_SHIP ? "Ship" : "Menu";
}

// ---- Runtime queries ------------------------------------------------

// An SDL device (by SDL device index) that is Steam's OWN emulation of a
// pad Steam Input already presents through the API — Valve's "Steam
// Virtual Gamepad", vendor 0x28DE product 0x11FF. When the Steam backend
// is active these are skipped on the SDL side (the entry point never
// opens them, enumeration never lists them), so a pad arrives exactly once
// (STEAMINPUT.md §5 rule 1, per DEVICE); a pad Steam does NOT present — one
// the player disabled Steam Input for, a device Steam does not model —
// stays a plain SDL pad. Always false when the Steam backend is inactive
// (then the virtual gamepad IS the pad, as on every shipped build).
bool pad_sdl_device_is_steam_virtual(int device_index);

// Connected right now (an id the backend still reports). PAD_NONE: false.
bool pad_attached(PadId id);
// Product name for logs — never NULL.
const char *pad_name(PadId id);
// Enumeration of the connected pads, backend order. The SDL half walks
// SDL's device list (opened or not, as the game always did); the Steam
// half walks its handle table.
int pad_count();
PadId pad_id_at(int index);
// 1-based position among the connected pads — stable enough to tell two
// pads apart on screen ("PAD 2"), and far shorter than product names. 0
// when the id is not connected.
int pad_number(PadId id);
// Raw SDL-scaled axis read (-32768..32767) for the menu's right-trigger
// poll. 0 for a Steam pad — its trigger arrives as a synthesized confirm
// edge, so the poll's job is already done.
int pad_axis(PadId id, int sdl_axis);

// Style (pad_style.h vocabulary), classified once per id and cached.
// PAD_NONE or an id never classified falls back to pad_style_any(), so a
// hint drawn for a seat without a pad still matches whatever pad IS
// plugged in.
PadStyle pad_style_for_id(PadId id);
// The most recently classified pad's style — hints that address no seat
// (the menu's PRESS START, the join invitation, the lobby's key line).
// Xbox when no pad has ever been seen (the NEWTONIA_PAD_STYLE override
// still applies, so a screenshot run needs no pad).
PadStyle pad_style_any();
// The most recently classified pad itself (PAD_NONE if none is attached).
PadId pad_any();
// Drop a removed pad's cache entries.
void pad_forget(PadId id);

// ---- Action glyphs (STEAMINPUT.md §4) --------------------------------
// The label of the position `a` sits on for this pad, in the pad's own
// vocabulary: an SDL pad answers from the table above (its bindings are
// the game's), a Steam pad from its CURRENT layout's origins — so a
// remapped A/B swaps the FIRE chip within a frame. One character for a
// face button (Typer::draw_button circles it), a word otherwise; "-" for
// an action the layout leaves unbound. PAD_NONE asks pad_any()'s pad.
// The pointer is valid until the next call for the same pad/action.
const char *pad_action_label(PadId id, PadAction a);
// The seatless variant: whichever pad was seen last.
const char *pad_action_label_any(PadAction a);
// Whether the action has a position at all on this pad — the zoom
// actions have none on the SDL path, so the F1 card omits their rows
// there and shows them for a Steam layout that binds them.
bool pad_action_bound(PadId id, PadAction a);

// Steam's overlay configurator for this pad's layout (ISteamInput::
// ShowBindingPanel). Only a Steam pad has one; false otherwise.
bool pad_has_binding_panel(PadId id);
bool pad_show_binding_panel(PadId id);

#endif
