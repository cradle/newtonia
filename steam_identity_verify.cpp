// Steam verification-credential backend for the netplay identity seam
// (net_identity.h V1) — compiled only under STEAM_BUILD, like the other
// steam_*.cpp TUs. Separate from steam_identity.cpp (which supplies the
// display name): this TU supplies the CREDENTIAL the signaling worker uses to
// ATTEST that name, closing the "self-reported claim" gap (NETPLAY.md V1).
//
// The credential is a Steam Web-API auth ticket minted by
// GetAuthTicketForWebApi("newtonia-signal"). It is submitted client->worker
// over wss only (never peer-to-peer), the worker validates it against Valve's
// publisher Web API (ISteamUserAuth/AuthenticateUserTicket — signal worker),
// and the attested persona is derived server-side from the proven SteamID.
// So nothing here puts an account ID on the peer wire (the XR-014 rule holds)
// and a lying name field on the p2p handshake simply stops mattering.
//
// The identity string passed to GetAuthTicketForWebApi MUST match the
// `identity` query parameter the worker passes to AuthenticateUserTicket
// (steam_verify.js) — Valve binds the ticket to it.

#ifdef STEAM_BUILD

#include <steam/steam_api.h>

#include <algorithm>
#include <string>
#include <vector>

#include "net_identity.h"

// Must equal STEAM_IDENTITY in signal/src/steam_verify.js / the worker env.
static const char *WEBAPI_IDENTITY = "newtonia-signal";

namespace {

// Owns the async ticket request and caches the completed hex. Ticket mint is
// asynchronous: GetAuthTicketForWebApi returns a handle immediately and the
// bytes arrive later via GetTicketForWebApiResponse_t (delivered on the game
// thread by SteamAPI_RunCallbacks, which the main loop already pumps). So
// local_verify_credential() returns the MOST RECENTLY completed ticket and
// fires a fresh request for next time — the first caller warms the cache
// (the lobby requests it on open, seconds before the join actually needs it).
//
// Web-API tickets are single-use: the worker's AuthenticateUserTicket
// consumes one, and a replay fails AuthTicketInvalidAlreadyUsed. Firing a
// fresh request per call keeps the cache from re-serving a spent ticket
// across sessions (a re-host / rejoin gets a new one).
//
// Handle cleanup (Steamworks asks callers to CancelAuthTicket every handle
// GetAuthTicketForWebApi returns): every minted handle is tracked in
// `handles_` and cancelled together in release(), called only when the
// netplay state chain ENDS (a lobby backing out to the menu, a net GLGame
// exiting) — never on a lobby->game or game->rejoin-lobby hand-off, whose
// successor still needs the warm ticket for reclaim/rejoin re-attests (see
// net_identity.h). It also cancels a ticket that was warmed but never sent
// (the lobby warms one on open; a player who backs out to the menu leaves it
// outstanding). Cancelling a ticket the worker already consumed is a harmless
// no-op. release() also clears hex_ so a subsequent send can never re-hand a
// ticket whose handle was just cancelled; the next credential() re-warms from
// scratch. A callback that lands after release (its handle no longer in
// handles_) is ignored, so a late arrival can't repopulate hex_ with an
// invalidated ticket.
class SteamTicketFetcher {
 public:
  SteamTicketFetcher()
      : response_cb_(this, &SteamTicketFetcher::on_response) {}

  const std::string &credential() {
    request();          // fire a fresh ticket for the next call
    return hex_;         // hand back the last one that completed ("" if none)
  }

  void release() {
    if (ISteamUser *user = SteamUser()) {
      for (HAuthTicket h : handles_) user->CancelAuthTicket(h);
    }
    handles_.clear();
    hex_.clear();
  }

 private:
  void request() {
    ISteamUser *user = SteamUser();
    if (!user) return;   // Steam client not up yet — try again next call
    // Multiple outstanding tickets are allowed. Track the handle so
    // release() can CancelAuthTicket it at teardown.
    HAuthTicket h = user->GetAuthTicketForWebApi(WEBAPI_IDENTITY);
    if (h == k_HAuthTicketInvalid) return;  // mint failed; hex_ unchanged
    handles_.push_back(h);
  }

  void on_response(GetTicketForWebApiResponse_t *r) {
    if (!r || r->m_eResult != k_EResultOK || r->m_cubTicket <= 0) return;
    // Ignore a completion for a handle we already released (cancelled) — its
    // ticket is invalid now, so it must not become hex_ and get sent.
    if (std::find(handles_.begin(), handles_.end(), r->m_hAuthTicket) ==
        handles_.end())
      return;
    int n = r->m_cubTicket;
    if (n > (int)sizeof(r->m_rgubTicket)) n = (int)sizeof(r->m_rgubTicket);
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve((size_t)n * 2);
    for (int i = 0; i < n; i++) {
      unsigned char b = r->m_rgubTicket[i];
      out += kHex[b >> 4];
      out += kHex[b & 0x0f];
    }
    hex_.swap(out);
  }

  CCallback<SteamTicketFetcher, GetTicketForWebApiResponse_t> response_cb_;
  std::string hex_;
  std::vector<HAuthTicket> handles_;  // outstanding, cancelled in release()
};

// One process-wide fetcher, shared by the mint and the teardown-cancel paths
// (both run on the game thread that pumps SteamAPI_RunCallbacks).
SteamTicketFetcher &fetcher() {
  static SteamTicketFetcher instance;
  return instance;
}

}  // namespace

namespace NetIdentityBackend {

std::string local_verify_credential() { return fetcher().credential(); }

void release_verify_credentials() { fetcher().release(); }

}  // namespace NetIdentityBackend

#endif  // STEAM_BUILD
