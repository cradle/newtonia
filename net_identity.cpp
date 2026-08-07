#include "net_identity.h"

#include <chrono>
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
std::string local_verify_credential_peek();
void release_verify_credentials();
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
  // Called only on the game thread (every netplay call site: the HELLO/WELCOME
  // append, the lobby/in-game send_identity), so the statics below need no
  // locking.
  static NetIdentity id;
  static bool platform_built = false;
  static bool name_built = false;
  if (!platform_built) {
    platform_built = true;
    id.platform = default_platform();
#ifdef IDENTITY_HAVE_BACKEND
    uint8_t backend_platform = NetIdentityBackend::local_platform();
    if (backend_platform != NET_PLATFORM_UNKNOWN) id.platform = backend_platform;
#endif
  }
  // The display name can arrive LATE and must NOT be frozen empty: a backend
  // whose name lookup is async (Play Games getCurrentPlayer, resolved after
  // sign-in) returns "" until it completes. Caching that "" for the process
  // would leave an Android peer role-labelled forever — invisible online (the
  // worker attests the real name from the verified player) but VISIBLE on a
  // LAN/offline session, where the CLAIMED name renders as-is with no worker
  // to fill it in. So retry the lookup on each call until it yields a name,
  // then freeze it (a resolved name is stable; env / no-backend builds resolve
  // on the first call, and a build with no name at all just keeps sending the
  // badge-only identity, retrying a cheap lookup each infrequent handshake).
  if (!name_built) {
    // Throttle the retry: the lookup was sized for handshake-frequency
    // callers, but the HUD badge row (Overlay::net_badges) now calls this
    // once per rendered frame — an unresolved name (Play Games before
    // sign-in) must not become a per-frame JNI round-trip. 2 s keeps the
    // retry-until-it-yields behaviour on a human timescale.
    static std::chrono::steady_clock::time_point last_try;
    auto now = std::chrono::steady_clock::now();
    if (last_try.time_since_epoch().count() != 0 &&
        now - last_try < std::chrono::seconds(2))
      return id;
    last_try = now;
    std::string name;
#ifdef IDENTITY_HAVE_BACKEND
    name = NetIdentityBackend::local_name();
#endif
    if (name.empty()) {
      // Dev/test hook (and stopgap until a name preference exists): no
      // placeholder default — a build without a name source sends the
      // badge-only identity (name_len 0) and the RECEIVER labels the peer
      // by role ("PLAYER 1" = host, "PLAYER 2" = client), which carries
      // strictly more information than a canned name could.
      const char *e = std::getenv("NEWTONIA_NET_NAME");
      if (e) name = e;
    }
    std::string clean = net_sanitize_name(name);
    if (!clean.empty()) {
      id.name = clean;
      name_built = true;  // resolved: freeze for the rest of the process
    }
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

std::string net_local_verify_credential_peek() {
#ifdef IDENTITY_HAVE_VERIFY
  return NetIdentityBackend::local_verify_credential_peek();
#else
  return "";
#endif
}

bool net_has_verify_backend() {
#ifdef IDENTITY_HAVE_VERIFY
  return true;
#else
  return false;
#endif
}

void net_release_verify_credentials() {
#ifdef IDENTITY_HAVE_VERIFY
  NetIdentityBackend::release_verify_credentials();
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

namespace {

// Decode one UTF-8 sequence beginning at raw[i]. On success advances i past
// the sequence and returns the codepoint. On ANY malformed byte (stray
// continuation, truncated/over-long/surrogate/out-of-range) advances i by a
// single byte and returns kBadCodepoint — the caller drops it, so bad input
// can neither desync the scan nor smuggle a byte through.
const uint32_t kBadCodepoint = 0xFFFFFFFFu;
uint32_t utf8_next(const std::string &raw, size_t &i) {
  unsigned char c0 = (unsigned char)raw[i];
  if (c0 < 0x80) { i += 1; return c0; }
  int len;
  uint32_t cp, min_cp;
  if ((c0 & 0xE0) == 0xC0)      { len = 2; cp = c0 & 0x1F; min_cp = 0x80; }
  else if ((c0 & 0xF0) == 0xE0) { len = 3; cp = c0 & 0x0F; min_cp = 0x800; }
  else if ((c0 & 0xF8) == 0xF0) { len = 4; cp = c0 & 0x07; min_cp = 0x10000; }
  else { i += 1; return kBadCodepoint; }  // lone continuation / invalid lead
  if (i + (size_t)len > raw.size()) { i += 1; return kBadCodepoint; }
  for (int k = 1; k < len; k++) {
    unsigned char cc = (unsigned char)raw[i + k];
    if ((cc & 0xC0) != 0x80) { i += 1; return kBadCodepoint; }  // truncated
    cp = (cp << 6) | (cc & 0x3F);
  }
  i += len;
  if (cp < min_cp) return kBadCodepoint;                       // overlong
  if (cp >= 0xD800 && cp <= 0xDFFF) return kBadCodepoint;      // UTF-16 surrogate
  if (cp > 0x10FFFF) return kBadCodepoint;                     // out of range
  return cp;
}

// Fold a non-ASCII codepoint to Typer-drawable ASCII, or return nullptr to
// drop it. Latin scripts collapse to their base letter so accented Western
// names survive intact ("JOSÉ" -> "JOSE", "Störmer" -> "STORMER") instead of
// silently losing characters; combining marks drop (their base already
// passed through); everything we can't render — Greek, Cyrillic, CJK, emoji,
// symbols — returns nullptr and the peer falls back to its role label. This
// is a RENDERING concern: the caller still gates every emitted byte through
// net_name_char_drawable, so a stray non-ASCII substitution could never leak.
const char *translit_codepoint(uint32_t cp) {
  // Latin-1 Supplement letters (U+00C0..U+00FF). "" marks the two non-letter
  // symbols in the range (× U+00D7, ÷ U+00F7), which drop.
  static const char *const kLatin1[] = {
    "A","A","A","A","A","A","AE","C","E","E","E","E","I","I","I","I",   // C0..CF
    "D","N","O","O","O","O","O","","O","U","U","U","U","Y","TH","ss",   // D0..DF (ß is lowercase)
    "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",   // E0..EF
    "d","n","o","o","o","o","o","","o","u","u","u","u","y","th","y",    // F0..FF
  };
  if (cp >= 0xC0 && cp <= 0xFF) {
    const char *s = kLatin1[cp - 0xC0];
    return s[0] ? s : nullptr;
  }
  // Latin Extended-A (U+0100..U+017F): Central/Eastern-European letters.
  switch (cp) {
    case 0x0100: case 0x0102: case 0x0104: return "A";
    case 0x0101: case 0x0103: case 0x0105: return "a";
    case 0x0106: case 0x0108: case 0x010A: case 0x010C: return "C";
    case 0x0107: case 0x0109: case 0x010B: case 0x010D: return "c";
    case 0x010E: case 0x0110: return "D";
    case 0x010F: case 0x0111: return "d";
    case 0x0112: case 0x0114: case 0x0116: case 0x0118: case 0x011A: return "E";
    case 0x0113: case 0x0115: case 0x0117: case 0x0119: case 0x011B: return "e";
    case 0x011C: case 0x011E: case 0x0120: case 0x0122: return "G";
    case 0x011D: case 0x011F: case 0x0121: case 0x0123: return "g";
    case 0x0124: case 0x0126: return "H";
    case 0x0125: case 0x0127: return "h";
    case 0x0128: case 0x012A: case 0x012C: case 0x012E: case 0x0130: return "I";
    case 0x0129: case 0x012B: case 0x012D: case 0x012F: case 0x0131: return "i";
    case 0x0134: return "J";
    case 0x0135: return "j";
    case 0x0136: return "K";
    case 0x0137: return "k";
    case 0x0139: case 0x013B: case 0x013D: case 0x013F: case 0x0141: return "L";
    case 0x013A: case 0x013C: case 0x013E: case 0x0140: case 0x0142: return "l";
    case 0x0143: case 0x0145: case 0x0147: return "N";
    case 0x0144: case 0x0146: case 0x0148: return "n";
    case 0x014C: case 0x014E: case 0x0150: return "O";
    case 0x014D: case 0x014F: case 0x0151: return "o";
    case 0x0152: return "OE";
    case 0x0153: return "oe";
    case 0x0154: case 0x0156: case 0x0158: return "R";
    case 0x0155: case 0x0157: case 0x0159: return "r";
    case 0x015A: case 0x015C: case 0x015E: case 0x0160: return "S";
    case 0x015B: case 0x015D: case 0x015F: case 0x0161: return "s";
    case 0x0162: case 0x0164: case 0x0166: return "T";
    case 0x0163: case 0x0165: case 0x0167: return "t";
    case 0x0168: case 0x016A: case 0x016C: case 0x016E: case 0x0170: case 0x0172: return "U";
    case 0x0169: case 0x016B: case 0x016D: case 0x016F: case 0x0171: case 0x0173: return "u";
    case 0x0174: return "W";
    case 0x0175: return "w";
    case 0x0176: case 0x0178: return "Y";
    case 0x0177: return "y";
    case 0x0179: case 0x017B: case 0x017D: return "Z";
    case 0x017A: case 0x017C: case 0x017E: return "z";
    case 0x017F: return "s";  // long s
  }
  // Combining diacritical marks (U+0300..U+036F) and anything else drop: the
  // decomposed base already emitted, so this discards the lone accent (== NFKD
  // then strip-marks), and every unrenderable script falls through to here.
  return nullptr;
}

}  // namespace

std::string net_sanitize_name(const std::string &raw) {
  std::string out;
  // Cap by DRAWABLE GLYPHS, not raw bytes: multi-byte UTF-8 must not count
  // double, and a fold can expand (ß -> SS, Œ -> OE). Output stays ASCII, so
  // it is still <= NET_IDENTITY_NAME_MAX bytes on the wire.
  size_t glyphs = 0;
  size_t i = 0;
  while (i < raw.size() && glyphs < (size_t)NET_IDENTITY_NAME_MAX) {
    uint32_t cp = utf8_next(raw, i);  // always advances i, even on bad bytes
    if (cp == kBadCodepoint) continue;

    if (cp < 0x80) {
      // SECURITY INVARIANT, deliberately independent of the glyph set:
      // control bytes (ESC/CSI, NUL, DEL) must never survive into the logs
      // or the display. The drawable check below is a rendering concern and
      // may evolve; this bound is the security boundary and must stay.
      if (cp < 0x20 || cp == 0x7f) continue;
      char c = (char)cp;
      if (net_name_char_drawable(c)) { out += c; glyphs++; }
      continue;
    }

    // Non-ASCII: fold Latin scripts to their ASCII base, drop the rest.
    // Nothing above 0x7f is ever emitted raw — every substitution byte is
    // re-checked against the drawable set, so the security boundary above
    // extends to the transliterated output too. A fold is atomic at the
    // glyph cap: an expanding substitution (Æ -> AE, ß -> ss) that doesn't
    // fit whole is dropped whole — truncating one mid-fold would render Æ
    // as a bare "A" as the name's last glyph.
    const char *sub = translit_codepoint(cp);
    if (!sub) continue;
    size_t sub_glyphs = 0;
    for (const char *p = sub; *p; p++)
      if (net_name_char_drawable(*p)) sub_glyphs++;
    if (glyphs + sub_glyphs > (size_t)NET_IDENTITY_NAME_MAX) break;
    for (const char *p = sub; *p; p++) {
      if (net_name_char_drawable(*p)) { out += *p; glyphs++; }
    }
  }
  // Trim surrounding spaces (dropped characters can leave stray separators).
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

// The ONE badge-composition rule ("NAME - LABEL"): empty label = name alone
// (a future/unshown platform), empty name = label alone (badge-only), both
// empty = "" (nothing renderable). Every badge composer goes through this so
// the HUD's local row and the peer rows below it can never drift in format.
std::string compose_badge(const std::string &name, const char *label) {
  if (!label[0]) return name;
  if (name.empty()) return label;
  return name + " - " + label;
}
}  // namespace

std::string net_identity_badge(const NetIdentity &id, NetIdentityCtx ctx) {
  bool show_plat = render_field(id.platform_trust, ctx);
  bool show_name = render_field(id.name_trust, ctx) && !id.name.empty();
  if (!show_plat && !show_name) return "";  // nothing renderable: no badge
  return compose_badge(show_name ? id.name : std::string(),
                       show_plat ? net_platform_label(id.platform) : "");
}

std::string net_identity_badge_or(const NetIdentity &id,
                                  const char *fallback_name,
                                  NetIdentityCtx ctx) {
  bool show_plat = render_field(id.platform_trust, ctx);
  bool show_name = render_field(id.name_trust, ctx) && !id.name.empty();
  // Nothing renders (legacy peer, or an online unattested peer): no badge,
  // no placeholder — the pre-badge UI stays exact.
  if (!show_plat && !show_name) return "";
  return compose_badge(show_name ? id.name : std::string(fallback_name),
                       show_plat ? net_platform_label(id.platform) : "");
}

std::string net_local_identity_badge(const char *fallback_name) {
  // Self-display: no render_field gate (see net_identity.h) — the same
  // compose_badge rule as the peer badges, minus the trust checks.
  const NetIdentity &id = net_local_identity();
  return compose_badge(id.name.empty() ? std::string(fallback_name) : id.name,
                       net_platform_label(id.platform));
}

std::string net_identity_name_or(const NetIdentity &id, const char *fallback,
                                 NetIdentityCtx ctx) {
  if (render_field(id.name_trust, ctx) && !id.name.empty()) return id.name;
  return fallback;  // unattested/withheld: role label wins
}

bool net_identity_verified(const NetIdentity &id, NetIdentityCtx ctx) {
  (void)ctx;  // an offline session has nothing attested; see net_identity.h
  return id.attested();
}
