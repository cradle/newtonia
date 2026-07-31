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

// TRUST — per field, not per identity (see NETPLAY.md V0). The platform tag
// and the display name each carry a trust level independently, so a partly
// attested identity (Game Center attests the account but not the name) is
// representable. APPEND ONLY.
enum NetTrust {
  // Nothing arrived for this field: a legacy peer, or a name withheld on the
  // wire. Never rendered.
  NET_TRUST_ABSENT = 0,
  // Self-reported on the peer-to-peer wire (the HELLO/WELCOME append). A
  // modified client can claim anything, so this is rendered ONLY on a
  // worker-less (LAN / manual-invite) session, where every peer was invited
  // into a local room and attestation is structurally impossible — the one
  // sanctioned carve-out (NETPLAY.md). On a worker session it renders as a
  // role label ("PLAYER 1"/"PLAYER 2"), never the claimed string.
  NET_TRUST_CLAIMED = 1,
  // Verified by the signaling worker against the platform's own backend
  // (Steam AuthenticateUserTicket + GetPlayerSummaries, etc.) and attested
  // to the room over the already-trusted signaling channel. Always rendered.
  NET_TRUST_ATTESTED = 2,
};

// Display context — decides whether a NET_TRUST_CLAIMED field renders.
//   NET_ID_ONLINE  : a signaling worker is (or was) in the session, so a
//                    stranger is possible and attestation is required —
//                    render Attested fields only, role labels otherwise.
//   NET_ID_OFFLINE : no worker (LAN / manual invite), attestation is
//                    structurally impossible and every peer was locally
//                    invited — the Claimed name/platform render as-is.
// The default is deliberately ONLINE (strict): a caller that forgets to pass
// the context can never leak an unattested claim, only under-render.
enum NetIdentityCtx {
  NET_ID_ONLINE = 0,
  NET_ID_OFFLINE = 1,
};

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

// Three first-class states, all of which every consumer must render:
//   badge+name  — platform and name both known (the common case)
//   badge-only  — platform known, name withheld (name_len 0 on the wire, a
//                 deliberate choice some platform backends make: display
//                 names are OPTIONAL; the platform tag alone carries the
//                 cross-network identifiability obligation). Render the
//                 badge plus the generic fallback name ("PLAYER 2") —
//                 never a placeholder like "UNKNOWN".
//   no identity — legacy peer, nothing appended: render exactly the
//                 pre-badge UI.
// The badge (net_identity_badge) and the name (net_identity_name_or) are
// therefore separable — no consumer may assume one implies the other.
struct NetIdentity {
  uint8_t platform;   // NetPlatform value
  std::string name;   // sanitized display name; empty = withheld/none
  uint8_t platform_trust;  // NetTrust for `platform`
  uint8_t name_trust;      // NetTrust for `name`
  NetIdentity()
      : platform(NET_PLATFORM_UNKNOWN),
        platform_trust(NET_TRUST_ABSENT),
        name_trust(NET_TRUST_ABSENT) {}
  // False for a legacy peer (nothing arrived on the wire) — the badge UX
  // must then render exactly the identity-less UI, no placeholder.
  bool known() const { return platform != NET_PLATFORM_UNKNOWN || !name.empty(); }
  // True once any field carries a worker attestation — the badge/HOSTED BY
  // row appears (and the greppable "net: identity attested" line is logged).
  bool attested() const {
    return platform_trust == NET_TRUST_ATTESTED ||
           name_trust == NET_TRUST_ATTESTED;
  }
};

// The local player's identity: compile-time platform detection plus the
// backend's display name. Without a name source the name stays EMPTY —
// the identity goes out badge-only (name_len 0) and the receiving side
// labels the peer by role: "PLAYER 1" for the host, "PLAYER 2" for the
// client (the fallback each name/badge call site passes). The
// NEWTONIA_NET_NAME env var is a dev/test name source for builds with no
// platform backend.
//
// Platform backends implement NetIdentityBackend::local_platform() /
// local_name() (see net_identity.cpp): STEAM_BUILD enables the Steam
// persona backend (steam_identity.cpp); any other platform defines
// NEWTONIA_NET_IDENTITY_BACKEND for its build and supplies the two
// functions in its own TU — the Xbox fork's gamertag backend returns
// NET_PLATFORM_XBOX + XUserGetGamertag there, with no edit to the shared
// layer. Backend returns platform 0 / empty name to keep the defaults.
const NetIdentity &net_local_identity();

// The local player's platform verification credential for the signaling
// worker to attest (NETPLAY.md V1): the Steam Web-API auth ticket (hex) from
// GetAuthTicketForWebApi under STEAM_BUILD, empty on every build without a
// verification backend. Submitted client->worker only (never peer-to-peer),
// so it carries no XR-014 concern. Backends supply
// NetIdentityBackend::local_verify_credential(); the default is "".
std::string net_local_verify_credential();

// Whether this build has a verification backend at all (STEAM_BUILD or a
// platform's NEWTONIA_NET_VERIFY_BACKEND). The credential itself is minted
// asynchronously and so may be "" transiently even with a backend present;
// this answers the STATIC question. The leaderboard gates its upload UI on
// it: a build that can never mint a credential can never pass the worker's
// attestation requirement, so it shows a view-only board rather than a
// doomed "UPLOAD" prompt (LEADERBOARD.md attestation decision).
bool net_has_verify_backend();

// Release any outstanding verification-credential handles the backend still
// holds — the Steamworks CancelAuthTicket cleanup for every handle
// GetAuthTicketForWebApi minted (Valve asks callers to cancel when done).
// Called ONLY at the END of the netplay state chain — a lobby that backs
// out / fails to the menu, or a net GLGame exiting — never on a hand-off
// between net states (lobby -> game, game -> auto-rejoin lobby). The
// successor state needs the warmed credential (a host reclaim re-attests
// with it, a rejoin lobby announces with it), and an early release can
// cancel a ticket the worker is still validating (a fast ICE connect
// reaches hand-off within the verify round-trip). It also mops up a ticket
// that was warmed but never sent (the lobby warms one on open, so a player
// who backs out to the menu leaves one outstanding). The backend also
// drops its cached credential, so a subsequent mint re-warms from scratch
// and a cancelled ticket can never be re-sent. A no-op on every build
// without a verification backend. Backends supply
// NetIdentityBackend::release_verify_credentials().
void net_release_verify_credentials();

// Merge a worker attestation into a peer identity built from the p2p wire.
// Each ATTESTED field in `attested` overwrites the corresponding field and
// promotes its trust; CLAIMED/ABSENT fields leave the existing value alone
// (the p2p claim, or a role fallback, stays). Used by the lobby/game when the
// worker's `identity` message arrives.
void net_apply_attested(NetIdentity &into, const NetIdentity &attested);

// Badge label for a platform tag ("STEAM", "WEB", ...); "" for Unknown and
// for values this build doesn't know (future platforms render name-only).
const char *net_platform_label(uint8_t platform);

// Decode UTF-8 and reduce a peer's raw display name to at most
// NET_IDENTITY_NAME_MAX Typer-drawable glyphs. Latin scripts are folded to
// their ASCII base so accented Western names survive ("JOSÉ" -> "JOSE",
// "Störmer" -> "STORMER") rather than losing characters; letters keep the
// shared upper/lower glyphs, so case is preserved. Everything the font can't
// render — Greek, Cyrillic, CJK, emoji, combining marks — is dropped, and a
// name that reduces to nothing renders as the peer's role label. Surrounding
// whitespace is trimmed. Control bytes and any un-folded non-ASCII are
// stripped explicitly, and every folded byte is re-gated through the drawable
// predicate — a security boundary (no terminal-escape/log injection) that
// must survive any future glyph-set growth. The output is ASCII, so it stays
// <= NET_IDENTITY_NAME_MAX bytes on the wire.
std::string net_sanitize_name(const std::string &raw);

// Every display helper takes the session context (NET_ID_ONLINE/OFFLINE):
// a field renders when it is ATTESTED, or when it is CLAIMED on an OFFLINE
// (worker-less) session. Online, an unattested claim renders as "" / the
// role fallback — never the claimed string.

// "GLENN - STEAM" (name + platform), "GLENN" (unknown label), "STEAM"
// (name filtered to nothing), or "" (nothing renderable — render no badge).
std::string net_identity_badge(const NetIdentity &id, NetIdentityCtx ctx);

// Like net_identity_badge but a nameless-yet-renderable peer gets the role
// fallback instead of a name-less badge: "PLAYER 1 - DESKTOP" (pass
// "PLAYER 1" when the peer is the host, "PLAYER 2" when it's the client).
// Still "" when nothing renders (legacy peer, or an online unattested peer).
std::string net_identity_badge_or(const NetIdentity &id,
                                  const char *fallback_name,
                                  NetIdentityCtx ctx);

// The peer's name, or `fallback` when its name doesn't render — the one rule
// for every name-bearing message ("GLENN DISCONNECTED" vs "PLAYER 2
// DISCONNECTED"), so the DISCONNECTED and RECONNECTED texts can't drift.
std::string net_identity_name_or(const NetIdentity &id, const char *fallback,
                                 NetIdentityCtx ctx);

// True when a badge for this identity should carry the verified tick: some
// field is worker-ATTESTED. The one rule for every badge site, so the HUD and
// the lobby can't drift apart. An OFFLINE (LAN / manual) session never ticks
// — its rendered fields are claims, which is the whole distinction the tick
// exists to draw — and that falls out of attestation being impossible there
// rather than being a second rule. Note the tick marks the ROW, not a field:
// Game Center attests the account but not the alias, so an iOS peer renders
// "PLAYER 2 - IOS" with a tick vouching for the platform badge, the role
// label being ours and never a claim.
bool net_identity_verified(const NetIdentity &id, NetIdentityCtx ctx);

// True when the Typer font can draw `c`. DEFINED IN typer.cpp, right next
// to the glyph table it must mirror, so a glyph addition updates both in
// one file; declared here so the SDL/GL-free net_identity.cpp can call it
// without pulling in typer.h's heavy includes.
bool net_name_char_drawable(char c);

#endif /* NET_IDENTITY_H */
