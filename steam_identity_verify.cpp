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

#include <string>

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
// across sessions (a re-host / rejoin gets a new one). A ticket that is
// warmed but never sent is simply never validated — harmless.
class SteamTicketFetcher {
 public:
  SteamTicketFetcher()
      : response_cb_(this, &SteamTicketFetcher::on_response) {}

  const std::string &credential() {
    request();          // fire a fresh ticket for the next call
    return hex_;         // hand back the last one that completed ("" if none)
  }

 private:
  void request() {
    ISteamUser *user = SteamUser();
    if (!user) return;   // Steam client not up yet — try again next call
    // Multiple outstanding tickets are allowed; we don't cancel the prior
    // handle (display-only stakes, and cancelling a still-in-flight ticket
    // the worker is validating would be counterproductive).
    HAuthTicket h = user->GetAuthTicketForWebApi(WEBAPI_IDENTITY);
    if (h == k_HAuthTicketInvalid) return;  // mint failed; hex_ unchanged
  }

  void on_response(GetTicketForWebApiResponse_t *r) {
    if (!r || r->m_eResult != k_EResultOK || r->m_cubTicket <= 0) return;
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
};

}  // namespace

namespace NetIdentityBackend {

std::string local_verify_credential() {
  static SteamTicketFetcher fetcher;
  return fetcher.credential();
}

}  // namespace NetIdentityBackend

#endif  // STEAM_BUILD
