// Controller glyph vocabulary — see pad_style.h.

#include "pad_style.h"

#include <map>

#include "steam_build.h"

static PadStyle style_from_steam_kind(SteamPadKind k, bool *known) {
  *known = true;
  switch (k) {
    case STEAM_PAD_PS4:   return PAD_STYLE_PS4;
    case STEAM_PAD_PS5:   return PAD_STYLE_PS5;
    case STEAM_PAD_OTHER: return PAD_STYLE_XBOX;
    default:              *known = false; return PAD_STYLE_XBOX;
  }
}

// ---------------------------------------------------------------- runtime

namespace {

struct Entry {
  PadStyle style;
  bool settled;       // no further Steam re-queries
  Uint32 first_ms;    // when the pad was first classified (retry window)
};

std::map<SDL_JoystickID, Entry> &cache() {
  static std::map<SDL_JoystickID, Entry> c;
  return c;
}

SDL_JoystickID g_last_id = -1;  // pad_style_any()

// Steam Input's answer can lag the pad's arrival (its controller list is
// populated by RunFrame, pumped from steam_run_callbacks), so an UNKNOWN
// from Steam keeps the SDL answer provisional and re-asks for a few
// seconds before settling on it.
const Uint32 STEAM_RETRY_MS = 5000;

PadStyle classify(SDL_GameController *c, bool *settled) {
  *settled = true;
  PadStyle forced;
  const char *env = SDL_getenv("NEWTONIA_PAD_STYLE");
  if (env && pad_style_parse(env, &forced)) return forced;

  Uint64 steam_handle = 0;
#if SDL_VERSION_ATLEAST(2, 30, 0)
  steam_handle = SDL_GameControllerGetSteamHandle(c);
#endif
  if (steam_input_available()) {
    bool known;
    PadStyle s = style_from_steam_kind(steam_pad_kind(steam_handle), &known);
    if (known) return s;
    *settled = false;
  }

  int sdl_type = 0;
#if SDL_VERSION_ATLEAST(2, 0, 12)
  sdl_type = (int)SDL_GameControllerGetType(c);
  if (sdl_type != (int)SDL_CONTROLLER_TYPE_UNKNOWN)
    return pad_style_from_sdl_type(sdl_type);
#endif
  return pad_style_from_name(SDL_GameControllerName(c));
}

}  // namespace

PadStyle pad_style_for(SDL_GameController *c) {
  if (!c) return pad_style_any();
  SDL_Joystick *j = SDL_GameControllerGetJoystick(c);
  if (!j) return pad_style_any();
  SDL_JoystickID id = SDL_JoystickInstanceID(j);
  std::map<SDL_JoystickID, Entry> &m = cache();
  std::map<SDL_JoystickID, Entry>::iterator it = m.find(id);
  Uint32 now = SDL_GetTicks();
  if (it != m.end()) {
    Entry &e = it->second;
    if (!e.settled) {
      bool settled;
      PadStyle s = classify(c, &settled);
      e.style = s;
      if (settled || now - e.first_ms > STEAM_RETRY_MS) e.settled = true;
    }
    g_last_id = id;
    return e.style;
  }
  Entry e;
  e.style = classify(c, &e.settled);
  e.first_ms = now;
  m[id] = e;
  g_last_id = id;
  return e.style;
}

PadStyle pad_style_for_id(SDL_JoystickID id) {
  if (id == -1) return pad_style_any();
  std::map<SDL_JoystickID, Entry> &m = cache();
  std::map<SDL_JoystickID, Entry>::iterator it = m.find(id);
  if (it != m.end() && it->second.settled) return it->second.style;
  SDL_GameController *c = SDL_GameControllerFromInstanceID(id);
  if (c) return pad_style_for(c);
  return it != m.end() ? it->second.style : pad_style_any();
}

PadStyle pad_style_any() {
  std::map<SDL_JoystickID, Entry> &m = cache();
  if (g_last_id != -1) {
    std::map<SDL_JoystickID, Entry>::iterator it = m.find(g_last_id);
    if (it != m.end()) return it->second.style;
  }
  if (!m.empty()) return m.begin()->second.style;
  // Nothing classified yet (a hint drawn before any pad was opened): the
  // override still applies, so a screenshot run needs no pad at all.
  PadStyle forced;
  const char *env = SDL_getenv("NEWTONIA_PAD_STYLE");
  if (env && pad_style_parse(env, &forced)) return forced;
  return PAD_STYLE_XBOX;
}

void pad_style_forget(SDL_JoystickID id) {
  cache().erase(id);
  if (g_last_id == id) g_last_id = -1;
}
