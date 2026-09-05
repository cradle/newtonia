// Steam Input — the physical pad behind a Steam-emulated controller (see
// steam_build.h and pad_style.h). Under Steam Input every configured pad
// reaches SDL as a virtual Xbox 360 controller, so SDL's own type query
// labels a DualSense with letters; ISteamInput knows what the player is
// actually holding. Nothing else of Steam Input is used — action sets,
// glyph PNGs and remapping stay Steam's business, and every pad still
// drives the game through SDL's GameController events.
// Compiled in every build; the non-Steam branch reports "unavailable".

#include <SDL.h>

#include "steam_build.h"

#ifdef STEAM_BUILD

namespace {
bool g_input_ready = false;

SteamPadKind kind_from_type(ESteamInputType t) {
  switch (t) {
    case k_ESteamInputType_PS3Controller:
    case k_ESteamInputType_PS4Controller:   return STEAM_PAD_PS4;
    case k_ESteamInputType_PS5Controller:   return STEAM_PAD_PS5;
    case k_ESteamInputType_Unknown:         return STEAM_PAD_UNKNOWN;
    default:                                return STEAM_PAD_OTHER;
  }
}
}  // namespace

void steam_input_init() {
  // bExplicitlyCallRunFrame = false: SteamAPI_RunCallbacks (pumped every
  // tick by steam_run_callbacks) drives RunFrame for us.
  g_input_ready = SteamInput() && SteamInput()->Init(false);
}

void steam_input_shutdown() {
  if (g_input_ready && SteamInput()) SteamInput()->Shutdown();
  g_input_ready = false;
}

bool steam_input_available() { return g_input_ready; }

SteamPadKind steam_pad_kind(uint64_t steam_handle) {
  if (!g_input_ready || !SteamInput()) return STEAM_PAD_UNKNOWN;
  if (steam_handle != 0)
    return kind_from_type(SteamInput()->GetInputTypeForHandle(
        (InputHandle_t)steam_handle));
  // No handle from SDL (pre-2.30 runtime): with exactly ONE pad on the
  // Steam side there is no ambiguity about which one this is. Two or more
  // and we can't tell — the caller falls back to SDL's answer.
  InputHandle_t handles[STEAM_INPUT_MAX_COUNT];
  int n = SteamInput()->GetConnectedControllers(handles);
  if (n == 1) return kind_from_type(SteamInput()->GetInputTypeForHandle(handles[0]));
  return STEAM_PAD_UNKNOWN;
}

#else  // !STEAM_BUILD

void steam_input_init() {}
void steam_input_shutdown() {}
bool steam_input_available() { return false; }
SteamPadKind steam_pad_kind(uint64_t steam_handle) {
  (void)steam_handle;
  return STEAM_PAD_UNKNOWN;
}

#endif
