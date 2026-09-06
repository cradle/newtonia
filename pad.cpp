// The pad seam — see pad.h. SDL half here; the Steam Input half is
// steam_input.cpp, consulted for ids from PAD_STEAM_BASE up. When it is
// active the two coexist per DEVICE: enumeration lists the Steam pads
// first, then every SDL pad that is not Steam's own virtual gamepad
// (pad_sdl_device_is_steam_virtual) — so a pad Steam presents arrives once,
// through the API, and a pad Steam does not present (Steam Input disabled
// for it) still arrives, through SDL.

#include "pad.h"
#include "steam_input.h"

#include <SDL.h>

#include <map>
#include <string>
#include <utility>

namespace {

// ---- style cache (moved in from the old pad_style.cpp) ----

std::map<PadId, PadStyle> &style_cache() {
  static std::map<PadId, PadStyle> c;
  return c;
}

PadId g_last_id = PAD_NONE;  // pad_any() / pad_style_any()

SDL_GameController *sdl_pad(PadId id) {
  if (id == PAD_NONE || pad_is_steam(id)) return NULL;
  return SDL_GameControllerFromInstanceID(id);
}

// SDL device indices the SDL half lists: game controllers, minus Steam's
// virtual gamepads while the Steam backend presents the real pads.
bool sdl_device_listed(int i) {
  return SDL_IsGameController(i) && !pad_sdl_device_is_steam_virtual(i);
}

bool forced_style(PadStyle *out) {
  const char *env = SDL_getenv("NEWTONIA_PAD_STYLE");
  return env && pad_style_parse(env, out);
}

// Classify an id the first time it is asked about: override, then the
// backend's type, then the name.
bool classify(PadId id, PadStyle *out) {
  if (forced_style(out)) return true;
  if (pad_is_steam(id)) {
    if (!steam_input_attached(id)) return false;
    *out = steam_input_style(id);
    return true;
  }
  SDL_GameController *c = sdl_pad(id);
  if (!c) return false;
#if SDL_VERSION_ATLEAST(2, 0, 12)
  int sdl_type = (int)SDL_GameControllerGetType(c);
  if (sdl_type != (int)SDL_CONTROLLER_TYPE_UNKNOWN) {
    *out = pad_style_from_sdl_type(sdl_type);
    return true;
  }
#endif
  *out = pad_style_from_name(SDL_GameControllerName(c));
  return true;
}

// ---- action-label cache ----
// A label pointer has to outlive the call, and a Steam pad's answer is a
// query into the client each time; refresh a pad/action pair no more than
// every LABEL_REFRESH_MS — well inside "within a frame or two" for a remap
// made in the overlay, and cheap enough for a HUD that asks every frame.
struct LabelEntry {
  std::string label;
  bool bound;
  Uint32 stamp;
};
std::map<std::pair<PadId, int>, LabelEntry> &label_cache() {
  static std::map<std::pair<PadId, int>, LabelEntry> c;
  return c;
}
const Uint32 LABEL_REFRESH_MS = 250;

void resolve_label(PadId id, PadAction a, LabelEntry &e) {
  const PadActionInfo &info = pad_action_info(a);
  if (pad_is_steam(id)) {
    int button = -1;
    const char *text = "";
    int n = steam_input_action_origin(id, a, &button, &text);
    if (n < 0) {
      // No bindings loaded for this pad yet (Steam applies them on
      // focus): the type's default position, not an "unbound" claim.
      e.bound = info.button != PAD_BUTTON_ZOOM_IN && info.button != PAD_BUTTON_ZOOM_OUT;
      e.label = e.bound ? pad_button_label(pad_style_for_id(id), info.button) : "-";
      return;
    }
    e.bound = n > 0;
    if (n <= 0) e.label = "-";
    else if (button >= 0) e.label = pad_button_label(pad_style_for_id(id), button);
    else e.label = text && *text ? text : "BUTTON";
    return;
  }
  // SDL pad (or no pad at all): the game's own positions. The zoom
  // actions have none here — nothing on the SDL path emits them.
  e.bound = info.button != PAD_BUTTON_ZOOM_IN && info.button != PAD_BUTTON_ZOOM_OUT;
  e.label = e.bound ? pad_button_label(pad_style_for_id(id), info.button) : "-";
}

const LabelEntry &label_for(PadId id, PadAction a) {
  std::map<std::pair<PadId, int>, LabelEntry> &m = label_cache();
  std::pair<PadId, int> key(id, (int)a);
  std::map<std::pair<PadId, int>, LabelEntry>::iterator it = m.find(key);
  Uint32 now = SDL_GetTicks();
  if (it == m.end()) {
    LabelEntry e;
    e.stamp = now;
    resolve_label(id, a, e);
    it = m.insert(std::make_pair(key, e)).first;
  } else if (now - it->second.stamp >= LABEL_REFRESH_MS) {
    it->second.stamp = now;
    resolve_label(id, a, it->second);
  }
  return it->second;
}

}  // namespace

bool pad_attached(PadId id) {
  if (id == PAD_NONE) return false;
  if (pad_is_steam(id)) return steam_input_attached(id);
  SDL_GameController *c = sdl_pad(id);
  return c != NULL && SDL_GameControllerGetAttached(c);
}

const char *pad_name(PadId id) {
  if (pad_is_steam(id)) return steam_input_name(id);
  SDL_GameController *c = sdl_pad(id);
  const char *n = c ? SDL_GameControllerName(c) : NULL;
  return n ? n : "?";
}

namespace {
// SDL instance id -> Steam Input handle (0 = probed, none). Filled by the
// entry point's per-device probe; entries leave with pad_forget.
std::map<int, unsigned long long> &sdl_steam_handles() {
  static std::map<int, unsigned long long> m;
  return m;
}
}  // namespace

bool pad_sdl_steam_handle_known(int instance_id) {
  return sdl_steam_handles().count(instance_id) > 0;
}

void pad_sdl_note_steam_handle(int instance_id, unsigned long long handle) {
  sdl_steam_handles()[instance_id] = handle;
}

unsigned long long pad_sdl_steam_handle(int instance_id) {
  std::map<int, unsigned long long>::iterator it = sdl_steam_handles().find(instance_id);
  return it == sdl_steam_handles().end() ? 0 : it->second;
}

bool pad_sdl_device_is_steam_virtual(int device_index) {
  if (!steam_input_active()) return false;
#if SDL_VERSION_ATLEAST(2, 0, 6)
  if (SDL_JoystickGetDeviceVendor(device_index) == 0x28DE &&
      SDL_JoystickGetDeviceProduct(device_index) == 0x11FF)
    return true;
#endif
  unsigned long long h = pad_sdl_steam_handle(SDL_JoystickGetDeviceInstanceID(device_index));
  if (h) return steam_input_owns_handle(h);
  // No handle: the physical pad behind one Steam is running. Steam is
  // running a pad when its API presents a handle OR when an SDL device
  // carries one (Steam's virtual gamepad — the API may present nothing
  // while that device exists, field 2026-09-06, and a template remap only
  // ever reaches the game through that device). The raw pad is redundant
  // once every pad Steam runs has a driver — adopted by the backend, or an
  // SDL device carrying its handle (see pad.h); kept while some pad has
  // none (the handle unreadable), and always when Steam runs none (Steam
  // Input off: the raw device IS the pad).
  int presented = steam_input_handle_count();
  int with_handle = 0;
  int n = SDL_NumJoysticks();
  for (int i = 0; i < n; i++)
    if (pad_sdl_steam_handle(SDL_JoystickGetDeviceInstanceID(i)) != 0) with_handle++;
  int running = presented > with_handle ? presented : with_handle;
  if (running == 0) return false;
  int drivers = steam_input_count() + with_handle;
  return drivers >= running;
}

int pad_count() {
  int pads = steam_input_count();
  int n = SDL_NumJoysticks();
  for (int i = 0; i < n; i++)
    if (sdl_device_listed(i)) pads++;
  return pads;
}

PadId pad_id_at(int index) {
  int steam = steam_input_count();
  if (index < steam) return steam_input_id_at(index);
  int n = SDL_NumJoysticks(), seen = steam;
  for (int i = 0; i < n; i++) {
    if (!sdl_device_listed(i)) continue;
    if (seen == index) return SDL_JoystickGetDeviceInstanceID(i);
    seen++;
  }
  return PAD_NONE;
}

int pad_number(PadId id) {
  if (id == PAD_NONE) return 0;
  int n = pad_count();
  for (int i = 0; i < n; i++)
    if (pad_id_at(i) == id) return i + 1;
  return 0;
}

int pad_axis(PadId id, int sdl_axis) {
  SDL_GameController *c = sdl_pad(id);
  if (!c) return 0;
  return SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)sdl_axis);
}

PadStyle pad_style_for_id(PadId id) {
  if (id == PAD_NONE) return pad_style_any();
  std::map<PadId, PadStyle> &m = style_cache();
  std::map<PadId, PadStyle>::iterator it = m.find(id);
  if (it != m.end()) {
    g_last_id = id;
    return it->second;
  }
  PadStyle s;
  if (!classify(id, &s)) return pad_style_any();
  m.insert(std::make_pair(id, s));
  g_last_id = id;
  return s;
}

PadStyle pad_style_any() {
  std::map<PadId, PadStyle> &m = style_cache();
  if (g_last_id != PAD_NONE) {
    std::map<PadId, PadStyle>::iterator it = m.find(g_last_id);
    if (it != m.end()) return it->second;
  }
  if (!m.empty()) return m.begin()->second;
  // Nothing classified yet (a hint drawn before any pad was seen): the
  // override still applies, so a screenshot run needs no pad at all.
  PadStyle forced;
  if (forced_style(&forced)) return forced;
  return PAD_STYLE_XBOX;
}

PadId pad_any() {
  if (g_last_id != PAD_NONE && pad_attached(g_last_id)) return g_last_id;
  // Fall back to whatever is connected, classifying it on the way so the
  // next call is direct.
  if (pad_count() > 0) {
    PadId id = pad_id_at(0);
    pad_style_for_id(id);
    return id;
  }
  return PAD_NONE;
}

void pad_forget(PadId id) {
  style_cache().erase(id);
  if (!pad_is_steam(id)) sdl_steam_handles().erase(id);
  std::map<std::pair<PadId, int>, LabelEntry> &m = label_cache();
  for (std::map<std::pair<PadId, int>, LabelEntry>::iterator it = m.begin();
       it != m.end();) {
    if (it->first.first == id) m.erase(it++);
    else ++it;
  }
  if (g_last_id == id) g_last_id = PAD_NONE;
}

const char *pad_action_label(PadId id, PadAction a) {
  if (id == PAD_NONE) id = pad_any();
  return label_for(id, a).label.c_str();
}

const char *pad_action_label_any(PadAction a) {
  return pad_action_label(pad_any(), a);
}

bool pad_action_bound(PadId id, PadAction a) {
  if (id == PAD_NONE) id = pad_any();
  return label_for(id, a).bound;
}

PadAction pad_action_or(PadId id, PadAction a, PadAction fallback) {
  return pad_action_bound(id, a) ? a : fallback;
}

bool pad_has_binding_panel(PadId id) {
  return pad_is_steam(id) && steam_input_attached(id);
}

bool pad_show_binding_panel(PadId id) {
  if (!pad_has_binding_panel(id)) return false;
  return steam_input_show_binding_panel(id);
}
