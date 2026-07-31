// Game Center (iOS) backend for the netplay peer-identity seam
// (net_identity.h, NETPLAY.md V3) — compiled only under
// __IOS__ && GAME_CENTER_BUILD (the iOS Xcode project sets both, and ios.yml's
// simulator build defines them too; the project/CI also define
// NEWTONIA_NET_IDENTITY_BACKEND + NEWTONIA_NET_VERIFY_BACKEND so
// net_identity.cpp calls the functions below). An empty translation unit on
// every other platform, exactly like game_center_achievements.mm.
//
// This is the iOS analogue of the two Steam TUs combined into one, mirroring
// the Android split-into-one (play_games_identity.cpp):
//   - steam_identity.cpp        -> the display name + platform tag
//   - steam_identity_verify.cpp -> the credential the worker attests with
//
//   - local_name()             -> GKLocalPlayer.alias (the public Game Center
//     username). Deliberately NEVER a Game Center account/team id
//     (net_identity.h, XR-014) — only the alias Game Center already shows
//     publicly. Read on the game thread; "" until sign-in resolves (the shared
//     layer retries until it does), and only ever RENDERED on a worker-less
//     (LAN/manual-invite) session — see the account-only note below.
//   - local_verify_credential()-> an identity-verification bundle from
//     GKLocalPlayer.fetchItemsForIdentityVerificationSignature (publicKeyURL,
//     signature, salt, timestamp) plus the local player's scoped identifiers
//     and the bundle id, packed as a compact JSON string. Submitted
//     client->worker over wss only (never peer-to-peer); the worker fetches
//     Apple's public key, rebuilds the signed byte string and verifies it
//     (signal/src/game_center_verify.js) to PROVE the account.
//
// ACCOUNT-ONLY ATTESTATION (Glenn, 2026-07-22): unlike Steam/Play Games, Apple
// exposes NO server-side lookup from a verified Game Center id to its alias,
// and the signature covers only identifier+bundleID+timestamp+salt — NOT the
// alias. So the worker proves the ACCOUNT (platform IOS attested) but attests
// an EMPTY name; an online iOS peer renders the IOS badge + its role label
// ("PLAYER 2"). The alias here is the OFFLINE/LAN claim only — kept, because
// on a worker-less session there is no attestation authority and every peer
// was locally invited (net_identity.h's sanctioned carve-out). Trusting a
// self-reported alias online would break the "attested names come only from a
// platform server" invariant Steam/Android hold, so we deliberately don't.
//
// The signed identifier ambiguity: Apple's docs/ecosystem disagree over whether
// fetchItemsForIdentityVerificationSignature binds the gamePlayerID or the
// teamPlayerID (iOS verification has never been device-tested here — NETPLAY.md
// M3-4). So BOTH are sent and the worker tries each; whichever verifies proves
// the account. Account-only attestation doesn't care which — it only needs one
// to check out.

#if defined(__IOS__) && defined(GAME_CENTER_BUILD)

#import <Foundation/Foundation.h>
#import <GameKit/GameKit.h>

#include <mutex>
#include <string>

#include "net_identity.h"

namespace {

// The last completed credential bundle (JSON) and an in-flight guard. The
// GameKit completion handler runs on an arbitrary queue, while the game thread
// reads credential(); a mutex keeps the std::string swap safe across them.
std::mutex g_mutex;
std::string g_credential;       // last completed bundle; "" until one lands
bool g_fetch_in_flight = false; // one outstanding fetch at a time

std::string to_std(NSString *s) {
  if (!s) return "";
  const char *c = [s UTF8String];
  return c ? std::string(c) : std::string();
}

// Kick a fresh identity-verification fetch on the main queue (GameKit wants its
// calls there) and cache the resulting bundle. Fires per credential() read so
// the bundle's timestamp stays inside the worker's freshness window (the signed
// blob has no single-use tracking — a tight window is the replay defence,
// game_center_verify.js). The in-flight guard prevents piling up fetches.
void kick_fetch() {
  dispatch_async(dispatch_get_main_queue(), ^{
    GKLocalPlayer *lp = [GKLocalPlayer localPlayer];
    if (!lp.isAuthenticated) {  // not signed in yet: retry on the next read
      std::lock_guard<std::mutex> lk(g_mutex);
      g_fetch_in_flight = false;
      return;
    }
    if (@available(iOS 13.5, *)) {
      // ObjC selector for the modern API (Swift: fetchItems(forIdentity
      // VerificationSignature:)) — the completion block is the unlabeled
      // argument; there is NO "WithCompletionHandler:" suffix (that was the
      // deprecated generateIdentityVerificationSignature… form).
      [lp fetchItemsForIdentityVerificationSignature:
          ^(NSURL *publicKeyURL, NSData *signature, NSData *salt,
            uint64_t timestamp, NSError *error) {
        if (error || !publicKeyURL || !signature || !salt) {
          std::lock_guard<std::mutex> lk(g_mutex);
          g_fetch_in_flight = false;
          return;
        }
        // Pack the bundle. NSJSONSerialization escapes every field correctly,
        // so the JSON survives being re-escaped into the wss identity frame
        // (NetSig::identity_frame). Both scoped identifiers ride along so the
        // worker can try each (see the header note). The alias is NOT included
        // — the worker never attests it (account-only); it travels as the
        // separate p2p `name` claim for the offline case.
        NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier];
        NSDictionary *d = @{
          @"pk": publicKeyURL.absoluteString ?: @"",
          @"sig": [signature base64EncodedStringWithOptions:0],
          @"salt": [salt base64EncodedStringWithOptions:0],
          @"ts": @(timestamp),
          @"gpid": lp.gamePlayerID ?: @"",
          @"tpid": lp.teamPlayerID ?: @"",
          @"bid": bundleId ?: @"",
        };
        NSData *json = [NSJSONSerialization dataWithJSONObject:d options:0
                                                        error:nil];
        std::lock_guard<std::mutex> lk(g_mutex);
        if (json && json.length > 0)
          g_credential.assign((const char *)json.bytes, json.length);
        g_fetch_in_flight = false;
      }];
    } else {
      // Pre-13.5 device: no fetchItems API. Leave the credential empty — the
      // peer simply stays unattested (role-labelled), and this is a vanishingly
      // small install base (deployment target is 13.0).
      std::lock_guard<std::mutex> lk(g_mutex);
      g_fetch_in_flight = false;
    }
  });
}

}  // namespace

namespace NetIdentityBackend {

uint8_t local_platform() { return NET_PLATFORM_IOS; }

// The Game Center alias, or "" until sign-in resolves (the shared layer retries
// each handshake until it yields a name). This is the OFFLINE/LAN claim only —
// online the worker attests an empty name (account-only), so an unresolved
// alias here is harmless online.
std::string local_name() {
  GKLocalPlayer *lp = [GKLocalPlayer localPlayer];
  if (!lp || !lp.isAuthenticated) return "";
  return to_std(lp.alias);
}

// The most recently completed verification bundle (JSON), or "" if none has
// landed yet, and fire a fresh fetch for next time. Mirrors
// steam_identity_verify.cpp / play_games_identity.cpp: return the warmed value,
// re-warm behind it. The lobby warms one on open, seconds before the join
// actually sends it.
std::string local_verify_credential() {
  std::string out;
  bool need_fetch = false;
  {
    std::lock_guard<std::mutex> lk(g_mutex);
    out = g_credential;
    if (!g_fetch_in_flight) {
      g_fetch_in_flight = true;
      need_fetch = true;
    }
  }
  if (need_fetch) kick_fetch();
  return out;
}

// Peek the last completed bundle WITHOUT firing a fresh fetch — the upload
// retry polls this to wait for a fresh-timestamp bundle after the first
// submit's local_verify_credential() read already kicked the re-fetch.
std::string local_verify_credential_peek() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return g_credential;
}

// Netplay teardown (~NetLobby / ~GLGame): drop any warmed-but-unsent bundle so a
// later session can't re-hand a stale one. There is no client-side handle to
// cancel (the signature is proven or rejected entirely server-side, unlike
// Steam's CancelAuthTicket), so this only clears the cache.
void release_verify_credentials() {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_credential.clear();
}

}  // namespace NetIdentityBackend

#endif  // __IOS__ && GAME_CENTER_BUILD
