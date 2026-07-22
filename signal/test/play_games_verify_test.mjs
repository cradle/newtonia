// Unit test for the Play Games identity verifier (NETPLAY.md V2): the worker
// redeems a client's single-use OAuth server auth code with Google and derives
// the display name from the VERIFIED player. Mocks the two Google endpoints
// (oauth2 token exchange, Play Games players/me) — no Google account involved.
// Run: node test/play_games_verify_test.mjs
import {
  looks_like_code,
  verifyPlayGamesCode,
  MAX_CODE_LEN,
} from "../src/play_games_verify.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}
function deep(name, a, b) { eq(name, JSON.stringify(a), JSON.stringify(b)); }

const ENV = {
  PLAY_GAMES_OAUTH_CLIENT_ID: "cid.apps.googleusercontent.com",
  PLAY_GAMES_OAUTH_CLIENT_SECRET: "shhh",
};
const CODE = "4/0AeanS0abc-DEF_123";

// A Google mock: `token` controls the oauth2 token exchange, `player` controls
// players/me. Records every URL (and token-exchange body) it was asked for.
function google(token, player) {
  const urls = [];
  const bodies = [];
  const fetcher = async (url, opts) => {
    urls.push(url);
    if (url.includes("oauth2.googleapis.com/token")) {
      bodies.push(opts && opts.body);
      return token;
    }
    if (url.includes("games/v1/players/me")) return player;
    return { ok: false };
  };
  return { fetcher, urls, bodies };
}
const okToken = (access_token) => ({
  ok: true, json: async () => ({ access_token, token_type: "Bearer" }),
});
const okPlayer = (playerId, displayName) => ({
  ok: true, json: async () => ({ playerId, displayName }),
});

// ---- looks_like_code ----
eq("code accepted", looks_like_code("4/0AeanS0abc-DEF_123"), true);
eq("empty rejected", looks_like_code(""), false);
eq("whitespace rejected", looks_like_code("abc def"), false);
eq("control char rejected", looks_like_code("abc\n"), false);
eq("non-string rejected", looks_like_code(1234), false);
eq("oversize rejected", looks_like_code("a".repeat(MAX_CODE_LEN + 1)), false);

// ---- happy path ----
{
  const { fetcher, urls, bodies } = google(okToken("ya29.token"),
                                           okPlayer("g1234567890", "GLENN"));
  const v = await verifyPlayGamesCode(ENV, CODE, fetcher);
  deep("valid code -> playerId + name", v,
       { playerId: "g1234567890", name: "GLENN" });
  eq("hits the token endpoint first", urls[0].includes("oauth2.googleapis.com/token"), true);
  // URLSearchParams form-encodes the code ("/" -> "%2F"), so it can't break
  // out of the body field.
  eq("token body carries the url-encoded code", bodies[0].includes("code=4%2F0AeanS0abc-DEF_123"), true);
  eq("token body carries the client id", bodies[0].includes("client_id=cid.apps.googleusercontent.com"), true);
  eq("token body carries the client secret", bodies[0].includes("client_secret=shhh"), true);
  eq("token body sets grant_type", bodies[0].includes("grant_type=authorization_code"), true);
  eq("then players/me", urls[1].includes("games/v1/players/me"), true);
}

// ---- unconfigured worker: attest nothing ----
eq("no client id -> null",
   await verifyPlayGamesCode({ PLAY_GAMES_OAUTH_CLIENT_SECRET: "x" }, CODE,
                             google(okToken("t"), okPlayer("1", "X")).fetcher), null);
eq("no client secret -> null",
   await verifyPlayGamesCode({ PLAY_GAMES_OAUTH_CLIENT_ID: "x" }, CODE,
                             google(okToken("t"), okPlayer("1", "X")).fetcher), null);

// ---- reused / expired code: token exchange 400 -> null ----
{
  const invalidGrant = { ok: false, json: async () => ({ error: "invalid_grant" }) };
  eq("reused code -> null",
     await verifyPlayGamesCode(ENV, CODE, google(invalidGrant, null).fetcher), null);
}

// ---- garbage code short-circuits before any fetch ----
{
  let called = false;
  await verifyPlayGamesCode(ENV, "not a code!", async () => { called = true; return { ok: false }; });
  eq("garbage code never calls Google", called, false);
}

// ---- token ok but missing access_token -> null ----
{
  const noToken = { ok: true, json: async () => ({ token_type: "Bearer" }) };
  eq("no access_token -> null",
     await verifyPlayGamesCode(ENV, CODE, google(noToken, null).fetcher), null);
}

// ---- Google HTTP error / network throw -> null ----
eq("token http error -> null",
   await verifyPlayGamesCode(ENV, CODE, async () => ({ ok: false })), null);
eq("token network throw -> null",
   await verifyPlayGamesCode(ENV, CODE, async () => { throw new Error("net"); }), null);

// ---- token proven but players/me fails: no verified id -> null ----
{
  const { fetcher } = google(okToken("ya29.token"), { ok: false });
  eq("players/me down -> null (no proven id)",
     await verifyPlayGamesCode(ENV, CODE, fetcher), null);
}

// ---- players/me ok but no playerId -> null ----
{
  const noId = { ok: true, json: async () => ({ displayName: "X" }) };
  eq("no playerId -> null",
     await verifyPlayGamesCode(ENV, CODE, google(okToken("t"), noId).fetcher), null);
}

// ---- player proven but no display name: attest the account, empty name ----
{
  const noName = { ok: true, json: async () => ({ playerId: "g99" }) };
  const v = await verifyPlayGamesCode(ENV, CODE, google(okToken("t"), noName).fetcher);
  deep("no displayName -> account attested, empty name", v,
       { playerId: "g99", name: "" });
}

process.exit(failures ? 1 : 0);
