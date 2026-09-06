#ifndef STEAM_INPUT_H
#define STEAM_INPUT_H

// The Steam Input API pad backend (STEAMINPUT.md §1 option A, §3, §5).
//
// On a STEAM_BUILD where ISteamInput::Init succeeded, Steam owns every
// controller: the game reads the two action sets of
// steam/game_actions_4536720.vdf once per tick and SYNTHESIZES the SDL
// controller events the consumers already speak — button down/up for a
// digital action's edge, LEFTX/LEFTY motion for the steer/nav stick —
// carrying a PadId from PAD_STEAM_BASE up. Hot-plug becomes
// StateManager::controller_added/removed on the same ids, so every
// FOURPLAYER.md behaviour (one input drives one ship, press-to-claim, the
// reconnect wait) runs unchanged. Set switching follows the state machine:
// State::pad_action_set() says which set the current screen wants, and
// the poll activates it on every pad the moment it changes, releasing
// whatever the outgoing set still held.
//
// The two rules of §5: (1) one owner per pad, kept per DEVICE — SDL's
// controller subsystem stays up beside this backend, and glut.cpp/pad.cpp
// skip Steam's own virtual gamepads (its emulation of the pads this API
// presents; pad_sdl_device_is_steam_virtual), so a pad Steam presents
// arrives once and a pad Steam does not present (Steam Input disabled for
// it) still arrives through SDL; (2) fallback is total — Init false (not
// launched by Steam, client too old, NEWTONIA_NO_STEAM,
// NEWTONIA_STEAM_INPUT=0) OR action sets Steam does not know (the In-Game
// Actions file not registered) means the SDL backend exactly as before,
// and nothing here is consulted again. Decided once at startup.
//
// Every decision traces through startup_trace (NEWTONIA_TRACE) — the only
// reliable output channel under Steam's runtime container (§9).
//
// Outside STEAM_BUILD everything here is an inline no-op.

#include "pad.h"

class StateManager;

// After steam_init() returned true. True when Steam Input owns the pads
// from here on.
bool steam_input_init();
bool steam_input_active();
// Once per tick, before game->tick: RunFrame, hot-plug diff, action reads
// -> synthetic events into the StateManager.
void steam_input_poll(StateManager *game);
void steam_input_shutdown();

// Backend queries for pad.cpp (ids are PAD_STEAM_BASE-relative there).
bool steam_input_attached(PadId id);
int steam_input_count();
PadId steam_input_id_at(int index);
const char *steam_input_name(PadId id);
// Style from ESteamInputType (PlayStation types -> shapes, else letters).
PadStyle steam_input_style(PadId id);
bool steam_input_show_binding_panel(PadId id);
// The layout's origin for an action: *button receives an
// SDL_GameControllerButton / PadPseudoButton when the first origin is a
// standard position, else -1 with *text naming it in Steam's words
// (a back grip, a trackpad). Returns the origin count (0 = unbound).
int steam_input_action_origin(PadId id, PadAction a, int *button,
                              const char **text);

#ifndef STEAM_BUILD
inline bool steam_input_init() { return false; }
inline bool steam_input_active() { return false; }
inline void steam_input_poll(StateManager *) {}
inline void steam_input_shutdown() {}
inline bool steam_input_attached(PadId) { return false; }
inline int steam_input_count() { return 0; }
inline PadId steam_input_id_at(int) { return PAD_NONE; }
inline const char *steam_input_name(PadId) { return "Steam pad"; }
inline PadStyle steam_input_style(PadId) { return PAD_STYLE_XBOX; }
inline bool steam_input_show_binding_panel(PadId) { return false; }
inline int steam_input_action_origin(PadId, PadAction, int *button,
                                     const char **text) {
  if (button) *button = -1;
  if (text) *text = "";
  return 0;
}
#endif

#endif
