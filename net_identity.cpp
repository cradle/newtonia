#include "net_identity.h"

#include <cstdlib>

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

// The verification-credential backend is a SEPARATE seam from the
// name/platform backend: a platform can supply a display identity without
// supplying an attestation credential (and vice versa). Steam supplies both
// (steam_identity.cpp names, steam_identity_verify.cpp the Web-API ticket);
// a future Xbox fork would define NEWTONIA_NET_VERIFY_BACKEND and provide
// NetIdentityBackend::local_verify_credential() in its own TU. Without either
// the credential is empty and the peer simply stays unattested.
#if defined(STEAM_BUILD) || defined(NEWTONIA_NET_VERIFY_BACKEND)
#define IDENTITY_HAVE_VERIFY 1
namespace NetIdentityBackend {
std::string local_verify_credential();
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
    if (name.empty()) {
      // Dev/test hook (and stopgap until a name preference exists): no
      // placeholder default — a build without a name backend sends the
      // badge-only identity (name_len 0) and the RECEIVER labels the peer
      // by role ("PLAYER 1" = host, "PLAYER 2" = client), which carries
      // strictly more information than a canned name could.
      const char *e = std::getenv("NEWTONIA_NET_NAME");
      if (e) name = e;
    }
    id.name = net_sanitize_name(name);
  }
  return id;
}

std::string net_local_verify_credential() {
#ifdef IDENTITY_HAVE_VERIFY
  return NetIdentityBackend::local_verify_credential();
#else
  return "";
#endif
}

void net_apply_attested(NetIdentity &into, const NetIdentity &attested) {
  if (attested.platform_trust == NET_TRUST_ATTESTED) {
    into.platform = attested.platform;
    into.platform_trust = NET_TRUST_ATTESTED;
  }
  if (attested.name_trust == NET_TRUST_ATTESTED) {
    into.name = attested.name;  // already sanitized on receipt
    into.name_trust = NET_TRUST_ATTESTED;
  }
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
    // SECURITY INVARIANT, deliberately independent of the glyph set:
    // control bytes (ESC/CSI, NUL, DEL) and non-ASCII must never survive
    // into the logs or the display, even if the Typer glyph table someday
    // grows entries outside printable ASCII. The drawable check below is
    // a rendering concern and may evolve; this line is the security
    // boundary and must stay.
    unsigned char u = (unsigned char)c;
    if (u < 0x20 || u >= 0x7f) continue;
    if (net_name_char_drawable(c)) out += c;
  }
  // Trim surrounding spaces (dropped UTF-8 can leave stray separators).
  size_t begin = out.find_first_not_of(' ');
  if (begin == std::string::npos) return "";
  size_t end = out.find_last_not_of(' ');
  return out.substr(begin, end - begin + 1);
}

namespace {
// A field renders when it is attested, or when it is a claim on a worker-less
// (offline) session — the one sanctioned carve-out (net_identity.h). An
// unattested claim on an online session never renders.
bool render_field(uint8_t trust, NetIdentityCtx ctx) {
  if (trust == NET_TRUST_ATTESTED) return true;
  return trust == NET_TRUST_CLAIMED && ctx == NET_ID_OFFLINE;
}
}  // namespace

std::string net_identity_badge(const NetIdentity &id, NetIdentityCtx ctx) {
  bool show_plat = render_field(id.platform_trust, ctx);
  bool show_name = render_field(id.name_trust, ctx) && !id.name.empty();
  if (!show_plat && !show_name) return "";  // nothing renderable: no badge
  std::string label = show_plat ? net_platform_label(id.platform) : "";
  if (!show_name) return label;       // may be "" — caller renders nothing
  if (label.empty()) return id.name;  // future/unshown platform: name-only
  return id.name + " - " + label;
}

std::string net_identity_badge_or(const NetIdentity &id,
                                  const char *fallback_name,
                                  NetIdentityCtx ctx) {
  bool show_plat = render_field(id.platform_trust, ctx);
  bool show_name = render_field(id.name_trust, ctx) && !id.name.empty();
  // Nothing renders (legacy peer, or an online unattested peer): no badge,
  // no placeholder — the pre-badge UI stays exact.
  if (!show_plat && !show_name) return "";
  std::string name = show_name ? id.name : std::string(fallback_name);
  std::string label = show_plat ? net_platform_label(id.platform) : "";
  if (label.empty()) return name;  // future/unshown platform: name-only badge
  return name + " - " + label;
}

std::string net_identity_name_or(const NetIdentity &id, const char *fallback,
                                 NetIdentityCtx ctx) {
  if (render_field(id.name_trust, ctx) && !id.name.empty()) return id.name;
  return fallback;  // unattested/withheld: role label wins
}
