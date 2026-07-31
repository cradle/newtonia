// Steam identity verification for the signaling worker (NETPLAY.md V1).
//
// The joiner/host submits a Steam Web-API auth ticket (minted client-side by
// GetAuthTicketForWebApi("newtonia-signal"), steam_identity_verify.cpp) with
// its room identity announcement. The worker — never the peer — validates it
// against Valve's publisher Web API, then derives the display name from the
// verified SteamID. A lying `name` field on the wire simply stops mattering:
// the attested name comes from Steam, not the client.
//
// Two calls, both to Valve, both needing the publisher Web API key (a
// Cloudflare secret, STEAM_WEBAPI_KEY, from a key group scoped to the app):
//   1. ISteamUserAuth/AuthenticateUserTicket — proves the ticket is genuine,
//      unexpired, single-use and owned by the returned SteamID.
//   2. ISteamUser/GetPlayerSummaries — the persona name for that SteamID.
//
// Nothing here is peer-to-peer: the ticket and SteamID travel client->worker
// over wss only, and only the display name is ever attested onward (the
// XR-014 "no account IDs on the wire" rule holds — see NETPLAY.md).

import { read_secret } from "./secret.js";

// Newtonia's Steam AppID (steam_build.h / STEAM_APPID). The ticket is bound
// to this app; AuthenticateUserTicket checks it.
export const STEAM_APPID = 4536720;

const AUTH_HOST = "https://partner.steam-api.com";
// Cap the accepted ticket hex so a peer can't flood the verifier. A Web-API
// ticket is ~1 KB binary => ~2 KB hex; 8 KB is generous headroom.
export const MAX_TICKET_HEX = 8192;

// Is `s` plausibly a hex-encoded ticket? (The client sends uppercase or
// lowercase hex; reject anything else before spending a Valve round-trip.)
export function looks_like_ticket(s) {
  return typeof s === "string" && s.length > 0 &&
         s.length <= MAX_TICKET_HEX && /^[0-9a-fA-F]+$/.test(s);
}

// Verify a Steam Web-API ticket and return { steamid, persona } on success,
// or null on any failure (bad ticket, reused, Valve down, missing key). The
// caller then attests { platform: steam, name: persona }; a null result
// leaves the peer unattested (role-labelled) — verification NEVER rejects.
//
// `fetcher` is injectable for tests (defaults to global fetch).
export async function verifySteamTicket(env, ticketHex, fetcher = fetch) {
  if (!env) return null;
  // Resolve the Web-API key up front — it may be a plain secret/var or a
  // Secrets Store binding (read_secret handles both). Empty = unconfigured.
  const webapi_key = await read_secret(env.STEAM_WEBAPI_KEY);
  if (!webapi_key) return null;                     // unconfigured: no attest
  if (!looks_like_ticket(ticketHex)) return null;
  const appid = Number(env.STEAM_APPID) || STEAM_APPID;

  // The `identity` MUST match GetAuthTicketForWebApi's pchIdentity on the
  // client (steam_identity_verify.cpp WEBAPI_IDENTITY) — Valve binds the
  // ticket to it and rejects a mismatch.
  const identity = env.STEAM_IDENTITY || "newtonia-signal";
  let steamid;
  try {
    const url = `${AUTH_HOST}/ISteamUserAuth/AuthenticateUserTicket/v1/` +
      `?key=${encodeURIComponent(webapi_key)}` +
      `&appid=${appid}&ticket=${encodeURIComponent(ticketHex)}` +
      `&identity=${encodeURIComponent(identity)}`;
    const resp = await fetcher(url);
    if (!resp.ok) return null;
    const data = await resp.json();
    const p = data && data.response && data.response.params;
    // Valve returns result:"OK" with a steamid on success; a reused ticket
    // (AuthTicketInvalidAlreadyUsed), an expired one, or a wrong app all come
    // back as an error object or result != "OK" — attest nothing.
    if (!p || p.result !== "OK" || !p.steamid) return null;
    // vacbanned / publisherbanned are advisory here (display-only stakes),
    // but a banned account still owns the ticket — we attest the name.
    steamid = String(p.steamid);
  } catch (e) {
    return null;
  }

  // Derive the display name from the verified SteamID — never the wire.
  let persona = "";
  try {
    const url = `${AUTH_HOST}/ISteamUser/GetPlayerSummaries/v2/` +
      `?key=${encodeURIComponent(webapi_key)}&steamids=${steamid}`;
    const resp = await fetcher(url);
    if (resp.ok) {
      const data = await resp.json();
      const players = data && data.response && data.response.players;
      if (Array.isArray(players) && players[0] &&
          typeof players[0].personaname === "string") {
        persona = players[0].personaname;
      }
    }
  } catch (e) {
    // Ticket proven but the persona lookup failed: still attest the account
    // (platform verified) with an empty name — the peer renders a role label
    // + the STEAM badge, which is strictly more than nothing.
    persona = "";
  }

  return { steamid, persona };
}
