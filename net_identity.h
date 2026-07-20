#ifndef NET_IDENTITY_H
#define NET_IDENTITY_H

// Netplay peer identity — platform-neutral seam following the
// presence/invites/achievements pattern (presence.h): a shared layer with a
// generic default backend, platform backends behind their own build flags
// (Steam: steam_identity.cpp under STEAM_BUILD).
//
// The identity is DISPLAY metadata only: a platform tag + a display name,
// exchanged as an append-only extension of the HELLO/WELCOME handshake
// (net_session.cpp) and rendered as a lobby/HUD badge. Never platform
// account IDs — no SteamID, no XUID, nothing persisted to disk, nothing in
// logs beyond the display name itself. An old peer that never sends an
// identity is still a fully playable partner (the badge just doesn't show).

#include <stdint.h>

#include <string>

// Wire-stable platform tags — APPEND ONLY, never renumber: these travel in
// the HELLO/WELCOME identity append and a mixed-version pairing must agree
// on what each value means. Values we don't know yet (a newer peer) render
// as name-only badges.
enum NetPlatform {
  NET_PLATFORM_UNKNOWN = 0,  // legacy peer (pre-identity build): no badge
  NET_PLATFORM_DESKTOP = 1,
  NET_PLATFORM_STEAM = 2,
  NET_PLATFORM_WEB = 3,
  NET_PLATFORM_IOS = 4,
  NET_PLATFORM_ANDROID = 5,
  NET_PLATFORM_XBOX = 6,  // reserved for the Xbox fork's private backend
};

// Display-name cap in bytes on the wire (excluding any terminator). Applied
// on send AND on receive — a peer's length claim is never trusted.
const int NET_IDENTITY_NAME_MAX = 24;

struct NetIdentity {
  uint8_t platform;   // NetPlatform value
  std::string name;   // sanitized display name; empty = none
  NetIdentity() : platform(NET_PLATFORM_UNKNOWN) {}
  // False for a legacy peer (nothing arrived on the wire) — the badge UX
  // must then render exactly the identity-less UI, no placeholder.
  bool known() const { return platform != NET_PLATFORM_UNKNOWN || !name.empty(); }
};

// The local player's identity: compile-time platform detection plus the
// backend's display name (generic "PLAYER" without a platform backend).
//
// Platform backends implement NetIdentityBackend::local_platform() /
// local_name() (see net_identity.cpp): STEAM_BUILD enables the Steam
// persona backend (steam_identity.cpp); any other platform defines
// NEWTONIA_NET_IDENTITY_BACKEND for its build and supplies the two
// functions in its own TU — the Xbox fork's gamertag backend returns
// NET_PLATFORM_XBOX + XUserGetGamertag there, with no edit to the shared
// layer. Backend returns platform 0 / empty name to keep the defaults.
const NetIdentity &net_local_identity();

// Badge label for a platform tag ("STEAM", "WEB", ...); "" for Unknown and
// for values this build doesn't know (future platforms render name-only).
const char *net_platform_label(uint8_t platform);

// Clamp to NET_IDENTITY_NAME_MAX bytes and keep only characters the Typer
// font can draw (letters map to the shared upper/lower glyphs, so case is
// preserved as-is); everything else — including multi-byte UTF-8 — is
// dropped. Surrounding whitespace is trimmed.
std::string net_sanitize_name(const std::string &raw);

// "GLENN - STEAM" (name + platform), "GLENN" (unknown label), "STEAM"
// (name filtered to nothing), or "" (no identity — render no badge).
std::string net_identity_badge(const NetIdentity &id);

// The peer's name, or `fallback` for a legacy/nameless peer — the one rule
// for every name-bearing message ("GLENN DISCONNECTED" vs "PLAYER 2
// DISCONNECTED"), so the DISCONNECTED and RECONNECTED texts can't drift.
std::string net_identity_name_or(const NetIdentity &id, const char *fallback);

// True when the Typer font can draw `c`. DEFINED IN typer.cpp, right next
// to the glyph table it must mirror, so a glyph addition updates both in
// one file; declared here so the SDL/GL-free net_identity.cpp can call it
// without pulling in typer.h's heavy includes.
bool net_name_char_drawable(char c);

#endif /* NET_IDENTITY_H */
