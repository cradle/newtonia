// Controller glyph vocabulary — see pad_style.h.

#include "pad_style.h"

#include <map>
#include <string>

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
  Uint64 steam_handle;  // SDL_GameControllerGetSteamHandle, 0 if none
  // Hint labels per SDL button, in the player's live Steam layout where
  // there is one, else the type table (see pad_hint_label).
  std::string labels[SDL_CONTROLLER_BUTTON_MAX];
  bool labels_valid;
  PadStyle labels_style;  // the style the labels were built in
  unsigned labels_gen;    // steam_input_config_generation at the last refresh
  Uint32 labels_ms;       // when the labels were last refreshed
  Entry() : style(PAD_STYLE_XBOX), settled(false), first_ms(0), steam_handle(0),
            labels_valid(false), labels_style(PAD_STYLE_XBOX), labels_gen(0),
            labels_ms(0) {}
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
// How often the live-layout labels are re-asked without a configuration
// callback — a backstop, the callback is the real trigger.
const Uint32 LABEL_REFRESH_MS = 2000;

Uint64 steam_handle_of(SDL_GameController *c) {
#if SDL_VERSION_ATLEAST(2, 30, 0)
  return SDL_GameControllerGetSteamHandle(c);
#else
  (void)c;
  return 0;
#endif
}

PadStyle classify(SDL_GameController *c, bool *settled) {
  *settled = true;
  PadStyle forced;
  const char *env = SDL_getenv("NEWTONIA_PAD_STYLE");
  if (env && pad_style_parse(env, &forced)) return forced;

  Uint64 steam_handle = steam_handle_of(c);
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
  e.steam_handle = steam_handle_of(c);
  m[id] = e;
  g_last_id = id;
  return e.style;
}

namespace {

// Rebuild an entry's labels: the type table, overridden per button by the
// live Steam layout where Steam Input answers for this pad.
void refresh_labels(Entry &e, Uint32 now) {
  for (int b = 0; b < (int)SDL_CONTROLLER_BUTTON_MAX; b++) {
    SDL_GameControllerButton btn = (SDL_GameControllerButton)b;
    e.labels[b] = pad_button_label(e.style, btn);
    int pos = -1;
    char text[64];
    if (steam_pad_origin(e.steam_handle, b, &pos, text, sizeof(text))) {
      if (pos >= 0 && pos < (int)SDL_CONTROLLER_BUTTON_MAX)
        e.labels[b] = pad_button_label(e.style, (SDL_GameControllerButton)pos);
      else if (text[0])
        e.labels[b] = text;
    }
  }
  e.labels_valid = true;
  e.labels_style = e.style;
  e.labels_gen = steam_input_config_generation();
  e.labels_ms = now;
}

}  // namespace

const char *pad_hint_label(SDL_JoystickID id, SDL_GameControllerButton b) {
  if (id == -1) return pad_hint_label_any(b);
  if ((int)b < 0 || (int)b >= (int)SDL_CONTROLLER_BUTTON_MAX)
    return pad_button_label(pad_style_for_id(id), b);
  // Make sure the pad is classified (and re-classified while unsettled).
  PadStyle style = pad_style_for_id(id);
  std::map<SDL_JoystickID, Entry> &m = cache();
  std::map<SDL_JoystickID, Entry>::iterator it = m.find(id);
  if (it == m.end()) return pad_button_label(style, b);
  Entry &e = it->second;
  Uint32 now = SDL_GetTicks();
  // Rebuilt when never built, when the classification settled on another
  // style (Steam's answer can land after the first frame), and — while
  // Steam Input drives the pad — on a configuration load or the backstop
  // timer. Off Steam the labels are the type table and never go stale.
  bool stale = !e.labels_valid || e.labels_style != e.style ||
               (steam_input_available() &&
                (e.labels_gen != steam_input_config_generation() ||
                 now - e.labels_ms > LABEL_REFRESH_MS));
  if (stale) refresh_labels(e, now);
  return e.labels[(int)b].c_str();
}

const char *pad_hint_label_any(SDL_GameControllerButton b) {
  std::map<SDL_JoystickID, Entry> &m = cache();
  SDL_JoystickID id = -1;
  if (g_last_id != -1 && m.find(g_last_id) != m.end()) id = g_last_id;
  else if (!m.empty()) id = m.begin()->first;
  if (id == -1) return pad_button_label(pad_style_any(), b);
  return pad_hint_label(id, b);
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
