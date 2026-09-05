#pragma once
// Steam SDK integration for Steam-distributed builds.
// Compiled only when STEAM_BUILD is defined (set by the deploy-steam workflow).
// All functions are safe no-ops on every other build.

#include <cstdlib>  // std::getenv (is_beta_feature_enabled)
#include <string>

#ifdef STEAM_BUILD
#include <steam/steam_api.h>
#endif

// Call once at startup, before the game is created.
// Returns false when the Steam client is not running; the caller should log a
// warning and continue — the game still works in offline / direct-launch mode.
// NEWTONIA_NO_STEAM=1 skips SteamAPI_Init entirely (every backend then
// takes its Steam-absent path) — the first bisection step when a launch
// through Steam misbehaves, since it separates "the Steam API" from
// "the binary/sandbox". Dev-only; no shipped launch sets it.
inline bool steam_init() {
#ifdef STEAM_BUILD
  if (std::getenv("NEWTONIA_NO_STEAM")) return false;
  return SteamAPI_Init();
#else
  return true;
#endif
}

// Call once at shutdown, after game objects have been destroyed.
inline void steam_shutdown() {
#ifdef STEAM_BUILD
  SteamAPI_Shutdown();
#endif
}

// Call once per game tick to let the Steam client dispatch queued callbacks.
inline void steam_run_callbacks() {
#ifdef STEAM_BUILD
  SteamAPI_RunCallbacks();
#endif
}

// Steam Deck: summon the floating on-screen keyboard, positioned to dock
// clear of the text field at the given window-relative pixel rect. The
// keyboard delivers ordinary key events (no read-back API needed), so the
// caller's normal keyboard path receives the typed characters. Returns
// false when the keyboard will not show — non-Steam build, Steam client
// absent, or hardware without a floating keyboard (plain desktop) — and
// the caller just leaves its own text-entry UI up.
inline bool steam_show_floating_keyboard(int x, int y, int w, int h) {
#ifdef STEAM_BUILD
  if (SteamUtils())
    return SteamUtils()->ShowFloatingGamepadTextInput(
        k_EFloatingGamepadTextInputModeModeSingleLine, x, y, w, h);
#endif
  (void)x; (void)y; (void)w; (void)h;
  return false;
}

// Dismiss the floating keyboard when leaving the text field. Harmless if
// it is not showing.
inline void steam_dismiss_floating_keyboard() {
#ifdef STEAM_BUILD
  if (SteamUtils()) SteamUtils()->DismissFloatingGamepadTextInput();
#endif
}

// One-shot poll: true once after the Deck's floating keyboard was
// dismissed (FloatingGamepadTextInputDismissed_t, dispatched by
// steam_run_callbacks). Lets the lobby bring its picker back the moment
// the keyboard closes instead of waiting for the next controller event
// to prove it. Always false on non-Steam builds. (steam_keyboard.cpp)
bool steam_floating_keyboard_dismissed();

// Returns the Steam beta branch the user is running on (e.g. "beta",
// "experimental"), or an empty string when on the default/public branch or
// when STEAM_BUILD is not defined.
inline std::string steam_get_branch() {
#ifdef STEAM_BUILD
  char branch[256] = {};
  if (SteamApps() && SteamApps()->GetCurrentBetaName(branch, sizeof(branch)))
    return std::string(branch);
#endif
  return std::string();
}

// Returns true when in-progress / beta-only features should be shown.
// Set NEWTONIA_BETA=1 in the environment to enable outside of Steam.
inline bool is_beta_feature_enabled() {
  // std::getenv, not SDL_getenv: this header is otherwise SDL-free (it must
  // stand alone — an isolated syntax-check has no SDL.h), and the codebase
  // already reads NEWTONIA_* dev flags with std::getenv in its SDL-free TUs.
  if (std::getenv("NEWTONIA_BETA")) return true;
  return steam_get_branch() == "beta";
}
