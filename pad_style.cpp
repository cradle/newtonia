// Controller glyph vocabulary — see pad_style.h.

#include "pad_style.h"

#include <map>

namespace {

std::map<SDL_JoystickID, PadStyle> &cache() {
  static std::map<SDL_JoystickID, PadStyle> c;
  return c;
}

SDL_JoystickID g_last_id = -1;  // pad_style_any()

PadStyle classify(SDL_GameController *c) {
  PadStyle forced;
  const char *env = SDL_getenv("NEWTONIA_PAD_STYLE");
  if (env && pad_style_parse(env, &forced)) return forced;
#if SDL_VERSION_ATLEAST(2, 0, 12)
  int sdl_type = (int)SDL_GameControllerGetType(c);
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
  std::map<SDL_JoystickID, PadStyle> &m = cache();
  std::map<SDL_JoystickID, PadStyle>::iterator it = m.find(id);
  if (it == m.end()) it = m.insert(std::make_pair(id, classify(c))).first;
  g_last_id = id;
  return it->second;
}

PadStyle pad_style_for_id(SDL_JoystickID id) {
  if (id == -1) return pad_style_any();
  std::map<SDL_JoystickID, PadStyle> &m = cache();
  std::map<SDL_JoystickID, PadStyle>::iterator it = m.find(id);
  if (it != m.end()) return it->second;
  SDL_GameController *c = SDL_GameControllerFromInstanceID(id);
  return c ? pad_style_for(c) : pad_style_any();
}

PadStyle pad_style_any() {
  std::map<SDL_JoystickID, PadStyle> &m = cache();
  if (g_last_id != -1) {
    std::map<SDL_JoystickID, PadStyle>::iterator it = m.find(g_last_id);
    if (it != m.end()) return it->second;
  }
  if (!m.empty()) return m.begin()->second;
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
