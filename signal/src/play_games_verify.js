// Play Games (Android) identity verification for the signaling worker
// (NETPLAY.md V2). The joiner/host submits a SINGLE-USE OAuth server auth code
// minted client-side by GamesSignInClient.requestServerSideAccess
// (PlayGamesIdentity.java) with its room identity announcement. The worker —
// never the peer — redeems it with Google using our OAuth client SECRET, then
// reads the VERIFIED player from the Play Games REST API. A lying `name` field
// on the wire simply stops mattering: the attested name comes from Google.
//
// Two calls, both to Google, both needing the OAuth web client id + secret
// (Cloudflare secrets PLAY_GAMES_OAUTH_CLIENT_ID / PLAY_GAMES_OAUTH_CLIENT_SECRET,
// from the Google Cloud project backing the Play Games "newtonia" game):
//   1. POST oauth2.googleapis.com/token — exchange the single-use auth code
//      for an access token (grant_type=authorization_code). A replayed or
//      expired code fails (invalid_grant): single-use is Google's job, exactly
//      like Steam's ticket AlreadyUsed.
//   2. GET games.googleapis.com/games/v1/players/me — the real player id +
//      display name for that token (Google's rule: never trust a client-
//      reported player id — derive it from the proven token).
//
// Nothing here is peer-to-peer: the code and player id travel client->worker
// over wss only, and only the display name is ever attested onward (the
// XR-014 "no account IDs on the wire" rule holds — see NETPLAY.md).

const TOKEN_HOST = "https://oauth2.googleapis.com/token";
const PLAYER_HOST = "https://games.googleapis.com/games/v1/players/me";

// Cap the accepted code so a peer can't flood the verifier. Google server
// auth codes are a few hundred chars; 2 KB is generous headroom (and well
// under the worker's MAX_IDENTITY_CRED frame bound).
export const MAX_CODE_LEN = 2048;

// Is `s` plausibly a Google OAuth server auth code? They are short URL-safe-ish
// strings (letters, digits and -._~/+=, e.g. the "4/0A…" shape). Reject
// control/whitespace and anything oversize before spending a Google round-trip.
export function looks_like_code(s) {
  return typeof s === "string" && s.length > 0 &&
         s.length <= MAX_CODE_LEN && /^[A-Za-z0-9\-._~/+=]+$/.test(s);
}

// Verify a Play Games server auth code and return { playerId, name } on
// success, or null on any failure (bad/expired/reused code, Google down,
// missing secret). The caller then attests { platform: android, name }; a null
// result leaves the peer unattested (role-labelled) — verification NEVER
// rejects the room.
//
// `fetcher` is injectable for tests (defaults to global fetch).
export async function verifyPlayGamesCode(env, code, fetcher = fetch) {
  if (!env || !env.PLAY_GAMES_OAUTH_CLIENT_ID ||
      !env.PLAY_GAMES_OAUTH_CLIENT_SECRET)
    return null;                       // unconfigured: attest nothing
  if (!looks_like_code(code)) return null;

  // 1. Redeem the single-use code for an access token.
  let accessToken;
  try {
    const body = new URLSearchParams({
      code,
      client_id: env.PLAY_GAMES_OAUTH_CLIENT_ID,
      client_secret: env.PLAY_GAMES_OAUTH_CLIENT_SECRET,
      grant_type: "authorization_code",
    });
    const resp = await fetcher(TOKEN_HOST, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: body.toString(),
    });
    // A reused/expired code comes back 400 invalid_grant — attest nothing.
    if (!resp.ok) return null;
    const data = await resp.json();
    if (!data || typeof data.access_token !== "string" || !data.access_token)
      return null;
    accessToken = data.access_token;
  } catch (e) {
    return null;
  }

  // 2. Read the VERIFIED player from the Play Games API — never the wire. The
  // proven token yields the authoritative player id + display name.
  try {
    const resp = await fetcher(PLAYER_HOST, {
      headers: { Authorization: `Bearer ${accessToken}` },
    });
    if (!resp.ok) return null;
    const data = await resp.json();
    if (!data || typeof data.playerId !== "string" || !data.playerId)
      return null;
    const name = typeof data.displayName === "string" ? data.displayName : "";
    return { playerId: data.playerId, name };
  } catch (e) {
    return null;
  }
}
