// Xbox identity verification for the signaling and board workers. The
// console signs its player in to Xbox, asks Xbox for a token addressed to
// PlayFab ("POST https://playfabapi.com/", the relying party PlayFab's own
// docs name for LoginWithXbox) and sends that token as its credential. The
// worker — never the peer — hands it to PlayFab's public LoginWithXbox
// endpoint for OUR PlayFab title. PlayFab decrypts and checks the token
// against the Xbox title linked to that PlayFab title, so a token minted by
// any other title fails, and answers with the proven Xbox user id.
//
// Only public PlayFab REST surface is used here (learn.microsoft.com,
// "Authentication - Login With Xbox"); the console side that mints the
// token lives in the Xbox fork.
//
// Configuration: PLAYFAB_TITLE_ID (a plain var or secret; read_secret takes
// both). Unset = unconfigured = attest nothing, like the other verifiers.
// No secret key is needed: LoginWithXbox is a client API whose proof is the
// token itself.
//
// PlayFab exposes no gamertag here (UserXboxInfo is the user id and
// sandbox), so like Game Center this proves the ACCOUNT only: the caller
// attests the platform and mints the ban token from the id, and the name
// stays unattested. The id itself never leaves the worker.

import { read_secret } from "./secret.js";

// Xbox tokens run to a few KB; stay under the workers' 8 KB credential cap.
export const MAX_TOKEN_LEN = 6144;

// "XBL3.0 x=<userhash>;<token>" — the exact string the console's token call
// returns. One space after the scheme, then printable ASCII with no space.
export function looks_like_token(s) {
  return typeof s === "string" && s.length > 9 &&
         s.length <= MAX_TOKEN_LEN && /^XBL3\.0 x=[\x21-\x7e]+;[\x21-\x7e]+$/.test(s);
}

// PlayFab title ids are short hex-ish strings; anything else must never be
// spliced into the request host.
export function looks_like_title_id(s) {
  return typeof s === "string" && /^[A-Za-z0-9]{1,16}$/.test(s);
}

// Verify an Xbox token through PlayFab. Returns { xuid, sandbox } on success,
// null on any failure (bad/expired token, title mismatch, PlayFab down,
// unconfigured). Verification never rejects a room: null just leaves the
// peer unattested. `fetcher` is injectable for tests.
export async function verifyXboxToken(env, token, fetcher = fetch) {
  if (!env) return null;
  const title_id = await read_secret(env.PLAYFAB_TITLE_ID);
  if (!looks_like_title_id(title_id)) return null;
  if (!looks_like_token(token)) return null;
  try {
    const resp = await fetcher(
        `https://${title_id}.playfabapi.com/Client/LoginWithXbox`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        TitleId: title_id,
        XboxToken: token,
        CreateAccount: true,
        InfoRequestParameters: { GetUserAccountInfo: true },
      }),
    });
    if (!resp.ok) {
      // PlayFab's error name (InvalidXboxLiveToken, ExpiredXboxLiveToken,
      // XboxRejectedXSTSExchangeRequest ...) is the one useful clue when a
      // console's token is refused; it carries nothing about the player.
      let err = "";
      try { err = String((await resp.json()).error || ""); } catch (e) {}
      console.log(`xbox verify refused status=${resp.status} ` +
                  `error=${err.slice(0, 64)}`);
      return null;
    }
    const body = await resp.json();
    const data = body && body.data;
    const info = data && data.InfoResultPayload &&
                 data.InfoResultPayload.AccountInfo &&
                 data.InfoResultPayload.AccountInfo.XboxInfo;
    if (!info || typeof info.XboxUserId !== "string" || !info.XboxUserId)
      return null;
    const sandbox = typeof info.XboxUserSandbox === "string"
                        ? info.XboxUserSandbox : "";
    return { xuid: info.XboxUserId, sandbox };
  } catch (e) {
    return null;
  }
}
