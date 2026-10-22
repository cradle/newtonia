// Unit test for the Xbox identity verifier: the worker hands a console's
// PlayFab-addressed Xbox token to PlayFab's public LoginWithXbox endpoint for
// our PlayFab title and takes the proven Xbox user id from the answer. Mocks
// PlayFab — no Xbox or PlayFab account involved.
// Run: node test/xbox_verify_test.mjs
import {
  looks_like_token,
  looks_like_title_id,
  verifyXboxToken,
  MAX_TOKEN_LEN,
} from "../src/xbox_verify.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}
function deep(name, a, b) { eq(name, JSON.stringify(a), JSON.stringify(b)); }

const ENV = { PLAYFAB_TITLE_ID: "A1B2C" };
const TOKEN = "XBL3.0 x=1234567890123456789;eyJlbmMiOiJBMTI4Q0JDLUhTMjU2In0.abc.def";

function playfab(resp) {
  const calls = [];
  const fetcher = async (url, opts) => { calls.push({ url, opts }); return resp; };
  return { fetcher, calls };
}
const okLogin = (XboxUserId, XboxUserSandbox) => ({
  ok: true,
  json: async () => ({ code: 200, status: "OK", data: {
    PlayFabId: "PF1", SessionTicket: "t",
    InfoResultPayload: { AccountInfo: { PlayFabId: "PF1",
      XboxInfo: { XboxUserId, XboxUserSandbox } } } } }),
});

// ---- shape checks ----
eq("token accepted", looks_like_token(TOKEN), true);
eq("missing scheme rejected", looks_like_token("x=1;abc"), false);
eq("missing userhash split rejected", looks_like_token("XBL3.0 x=abc"), false);
eq("inner space rejected", looks_like_token("XBL3.0 x=1;ab c"), false);
eq("newline rejected", looks_like_token("XBL3.0 x=1;abc\n"), false);
eq("non-string rejected", looks_like_token(42), false);
eq("oversize rejected",
   looks_like_token("XBL3.0 x=1;" + "a".repeat(MAX_TOKEN_LEN)), false);
eq("title id accepted", looks_like_title_id("A1B2C"), true);
eq("title id host injection rejected", looks_like_title_id("evil.com/x"), false);
eq("empty title id rejected", looks_like_title_id(""), false);

// ---- happy path ----
{
  const { fetcher, calls } = playfab(okLogin("2535400000000001", "DQPXFB.0"));
  const v = await verifyXboxToken(ENV, TOKEN, fetcher);
  deep("verified xuid + sandbox", v, { xuid: "2535400000000001", sandbox: "DQPXFB.0" });
  eq("one call", calls.length, 1);
  eq("title-scoped endpoint", calls[0].url,
     "https://A1B2C.playfabapi.com/Client/LoginWithXbox");
  eq("POST", calls[0].opts.method, "POST");
  const body = JSON.parse(calls[0].opts.body);
  eq("token forwarded verbatim", body.XboxToken, TOKEN);
  eq("title id in body", body.TitleId, "A1B2C");
  eq("account info requested", body.InfoRequestParameters.GetUserAccountInfo, true);
}

// ---- secrets store binding shape ----
{
  const env = { PLAYFAB_TITLE_ID: { get: async () => "A1B2C" } };
  const { fetcher } = playfab(okLogin("99", ""));
  deep("store binding title id", await verifyXboxToken(env, TOKEN, fetcher),
       { xuid: "99", sandbox: "" });
}

// ---- failures attest nothing ----
{
  const { fetcher, calls } = playfab(okLogin("1", ""));
  eq("unconfigured -> null", await verifyXboxToken({}, TOKEN, fetcher), null);
  eq("bad title id -> null",
     await verifyXboxToken({ PLAYFAB_TITLE_ID: "a.b" }, TOKEN, fetcher), null);
  eq("malformed token -> null", await verifyXboxToken(ENV, "garbage", fetcher), null);
  eq("no env -> null", await verifyXboxToken(null, TOKEN, fetcher), null);
  eq("no PlayFab call on any of those", calls.length, 0);
}
{
  const { fetcher } = playfab({ ok: false, status: 400,
    json: async () => ({ code: 400, error: "InvalidXboxLiveToken" }) });
  eq("refused token -> null", await verifyXboxToken(ENV, TOKEN, fetcher), null);
}
{
  const { fetcher } = playfab({ ok: true, json: async () => ({ data: {
    InfoResultPayload: { AccountInfo: { PlayFabId: "PF1" } } } }) });
  eq("no XboxInfo -> null", await verifyXboxToken(ENV, TOKEN, fetcher), null);
}
{
  const { fetcher } = playfab(okLogin("", ""));
  eq("empty xuid -> null", await verifyXboxToken(ENV, TOKEN, fetcher), null);
}
{
  const fetcher = async () => { throw new Error("network"); };
  eq("network error -> null", await verifyXboxToken(ENV, TOKEN, fetcher), null);
}

if (failures) { console.log(`${failures} FAILED`); process.exit(1); }
console.log("all xbox verify tests passed");
