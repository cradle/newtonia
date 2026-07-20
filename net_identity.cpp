#include "net_identity.h"

// Shared layer of the peer-identity seam (net_identity.h). Compiles in every
// build unconditionally, like the other net_*.cpp shared layers — no SDL, no
// GL, no platform SDK includes here (the glyph predicate net_name_char_drawable
// is defined in typer.cpp beside the glyph table for the same reason).

// Platform backends implement these two behind their own build flag, in
// their own TU: STEAM_BUILD -> steam_identity.cpp (persona name);
// NEWTONIA_NET_IDENTITY_BACKEND -> any other platform's backend (the Xbox
// fork's gamertag backend lands here with no edit to this file). A backend
// returns platform 0 / empty name to fall back to the defaults below.
#if defined(STEAM_BUILD) || defined(NEWTONIA_NET_IDENTITY_BACKEND)
#define IDENTITY_HAVE_BACKEND 1
namespace NetIdentityBackend {
uint8_t local_platform();
std::string local_name();
}
#endif

namespace {

// Compile-time platform detection — the no-backend default. Uses the
// codebase's own platform macros (__IOS__ from ios/project.yml, the same
// macro gl_compat.h and touch_controls branch on).
uint8_t default_platform() {
#if defined(__EMSCRIPTEN__)
  return NET_PLATFORM_WEB;
#elif defined(__ANDROID__)
  return NET_PLATFORM_ANDROID;
#elif defined(__IOS__)
  return NET_PLATFORM_IOS;
#else
  return NET_PLATFORM_DESKTOP;
#endif
}

}  // namespace

const NetIdentity &net_local_identity() {
  static NetIdentity id;
  static bool built = false;
  if (!built) {
    built = true;
    id.platform = default_platform();
    std::string name;
#ifdef IDENTITY_HAVE_BACKEND
    uint8_t backend_platform = NetIdentityBackend::local_platform();
    if (backend_platform != NET_PLATFORM_UNKNOWN) id.platform = backend_platform;
    name = NetIdentityBackend::local_name();
#endif
    if (name.empty()) name = "PLAYER";
    id.name = net_sanitize_name(name);
  }
  return id;
}

const char *net_platform_label(uint8_t platform) {
  switch (platform) {
    case NET_PLATFORM_DESKTOP: return "DESKTOP";
    case NET_PLATFORM_STEAM:   return "STEAM";
    case NET_PLATFORM_WEB:     return "WEB";
    case NET_PLATFORM_IOS:     return "IOS";
    case NET_PLATFORM_ANDROID: return "ANDROID";
    case NET_PLATFORM_XBOX:    return "XBOX";
  }
  return "";  // Unknown/legacy, and platforms newer than this build
}

std::string net_sanitize_name(const std::string &raw) {
  std::string out;
  for (size_t i = 0; i < raw.size() && out.size() < (size_t)NET_IDENTITY_NAME_MAX;
       i++) {
    char c = raw[i];
    if (net_name_char_drawable(c)) out += c;
  }
  // Trim surrounding spaces (dropped UTF-8 can leave stray separators).
  size_t begin = out.find_first_not_of(' ');
  if (begin == std::string::npos) return "";
  size_t end = out.find_last_not_of(' ');
  return out.substr(begin, end - begin + 1);
}

std::string net_identity_badge(const NetIdentity &id) {
  std::string label = net_platform_label(id.platform);
  if (id.name.empty()) return label;  // may be "" — caller renders nothing
  if (label.empty()) return id.name;  // future platform: name-only badge
  return id.name + " - " + label;
}

std::string net_identity_name_or(const NetIdentity &id, const char *fallback) {
  return id.name.empty() ? std::string(fallback) : id.name;
}
