#include "net_identity.h"

// Shared layer of the peer-identity seam (net_identity.h). Compiles in every
// build unconditionally, like the other net_*.cpp shared layers — no SDL, no
// GL, no platform SDK includes here.

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#ifdef STEAM_BUILD
// steam_identity.cpp (compiled only under STEAM_BUILD) provides the persona
// name; the same flag selects the Steam platform tag below.
namespace NetIdentityBackend {
std::string local_name();
}
#endif

namespace {

// Compile-time platform detection, mirroring how the build system separates
// the platforms (each flag is exclusive to its build).
uint8_t local_platform() {
#if defined(__EMSCRIPTEN__)
  return NET_PLATFORM_WEB;
#elif defined(__ANDROID__)
  return NET_PLATFORM_ANDROID;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  return NET_PLATFORM_IOS;
#elif defined(STEAM_BUILD)
  return NET_PLATFORM_STEAM;
#else
  return NET_PLATFORM_DESKTOP;
#endif
}

std::string local_name() {
#ifdef STEAM_BUILD
  std::string name = NetIdentityBackend::local_name();
  if (!name.empty()) return name;
#endif
  return "PLAYER";
}

// Characters the Typer font has glyphs for (typer.cpp init_meshes): letters
// (upper/lower share meshes), digits, and this symbol set. Space advances
// without drawing, which is fine in a name.
bool typer_can_draw(char c) {
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= 'a' && c <= 'z') return true;
  if (c >= '0' && c <= '9') return true;
  switch (c) {
    case ' ': case '-': case '.': case ',': case '+': case '/':
    case '(': case ')': case '[': case ']': case '<': case '>': case '=':
      return true;
  }
  return false;
}

}  // namespace

const NetIdentity &net_local_identity() {
  static NetIdentity id;
  static bool built = false;
  if (!built) {
    built = true;
    id.platform = local_platform();
    id.name = net_sanitize_name(local_name());
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
    if (typer_can_draw(c)) out += c;
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
