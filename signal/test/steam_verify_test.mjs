// Unit test for the Steam identity verifier (NETPLAY.md V1): the worker
// validates a client's Web-API auth ticket against Valve and derives the
// display name from the proven SteamID. Mocks the two Valve endpoints
// (AuthenticateUserTicket, GetPlayerSummaries) — no Steam account involved.
// Run: node test/steam_verify_test.mjs
import {
  looks_like_ticket,
  verifySteamTicket,
  STEAM_APPID,
  MAX_TICKET_HEX,
} from "../src/steam_verify.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}
function deep(name, a, b) { eq(name, JSON.stringify(a), JSON.stringify(b)); }

const ENV = { STEAM_WEBAPI_KEY: "PUBKEY" };
const TICKET = "deadbeef01";

// A Valve mock: `auth` controls AuthenticateUserTicket, `summary` controls
// GetPlayerSummaries. Records every URL it was asked for.
function valve(auth, summary) {
  const urls = [];
  const fetcher = async (url) => {
    urls.push(url);
    if (url.includes("AuthenticateUserTicket")) return auth;
    if (url.includes("GetPlayerSummaries")) return summary;
    return { ok: false };
  };
  return { fetcher, urls };
}
const okAuth = (steamid) => ({
  ok: true, json: async () => ({ response: { params: { result: "OK", steamid } } }),
});
const okSummary = (steamid, personaname) => ({
  ok: true, json: async () => ({ response: { players: [{ steamid, personaname }] } }),
});

// ---- looks_like_ticket ----
eq("hex ticket accepted", looks_like_ticket("00ABff"), true);
eq("empty rejected", looks_like_ticket(""), false);
eq("non-hex rejected", looks_like_ticket("xyz"), false);
eq("non-string rejected", looks_like_ticket(1234), false);
eq("oversize rejected", looks_like_ticket("a".repeat(MAX_TICKET_HEX + 1)), false);

// ---- happy path ----
{
  const { fetcher, urls } = valve(okAuth("76561198000000001"),
                                  okSummary("76561198000000001", "GLENN"));
  const v = await verifySteamTicket(ENV, TICKET, fetcher);
  deep("valid ticket -> steamid + persona", v,
       { steamid: "76561198000000001", persona: "GLENN" });
  eq("hits AuthenticateUserTicket first", urls[0].includes("AuthenticateUserTicket"), true);
  eq("passes the publisher key", urls[0].includes("key=PUBKEY"), true);
  eq("passes our appid", urls[0].includes(`appid=${STEAM_APPID}`), true);
  eq("binds the identity string", urls[0].includes("identity=newtonia-signal"), true);
  eq("then GetPlayerSummaries for the steamid",
     urls[1].includes("GetPlayerSummaries") && urls[1].includes("steamids=76561198000000001"), true);
}

// ---- unconfigured worker: attest nothing ----
eq("no publisher key -> null",
   await verifySteamTicket({}, TICKET, valve(okAuth("1"), okSummary("1", "X")).fetcher), null);

// ---- malformed / reused / wrong-app ticket: result != OK -> null ----
{
  const reused = { ok: true, json: async () => ({ response: { error: { errorcode: 4, errordesc: "AlreadyUsed" } } }) };
  eq("reused ticket -> null",
     await verifySteamTicket(ENV, TICKET, valve(reused, null).fetcher), null);
  const notOk = { ok: true, json: async () => ({ response: { params: { result: "Expired" } } }) };
  eq("non-OK result -> null",
     await verifySteamTicket(ENV, TICKET, valve(notOk, null).fetcher), null);
}

// ---- garbage ticket short-circuits before any fetch ----
{
  let called = false;
  await verifySteamTicket(ENV, "not-hex!", async () => { called = true; return { ok: false }; });
  eq("garbage ticket never calls Valve", called, false);
}

// ---- Valve HTTP error / network throw -> null ----
eq("auth http error -> null",
   await verifySteamTicket(ENV, TICKET, async () => ({ ok: false })), null);
eq("auth network throw -> null",
   await verifySteamTicket(ENV, TICKET, async () => { throw new Error("net"); }), null);

// ---- ticket proven but persona lookup fails: attest the account, empty name ----
{
  const { fetcher } = valve(okAuth("76561198000000009"), { ok: false });
  const v = await verifySteamTicket(ENV, TICKET, fetcher);
  deep("persona lookup down -> account attested, empty name", v,
       { steamid: "76561198000000009", persona: "" });
}
{
  // Summaries returns an empty player list (private profile): same fallback.
  const emptyList = { ok: true, json: async () => ({ response: { players: [] } }) };
  const { fetcher } = valve(okAuth("76561198000000010"), emptyList);
  const v = await verifySteamTicket(ENV, TICKET, fetcher);
  deep("empty summaries -> empty name", v,
       { steamid: "76561198000000010", persona: "" });
}

process.exit(failures ? 1 : 0);
