# Newtonia signaling worker

Cloudflare Worker + Durable Objects that mints 4-letter room codes and
relays the WebRTC offer/answer between a host and up to three joiners
(FOURPLAYER.md PB-D5: joiners are tagged with per-room monotonic ids;
host frames may address a joiner with `to`, joiner frames reach the host
stamped `from`; an unaddressed offer keeps the legacy single-pair
semantics). Protocol and milestone context: `../NETPLAY.md` (Milestone 2)
and `../FOURPLAYER.md` §4.

## Local development / e2e

No Cloudflare account needed:

```sh
cd signal
npx wrangler dev --local --port 8787
```

The game connects to `ws://127.0.0.1:8787/ws` when the `signal_url`
preference (or `NEWTONIA_SIGNAL_URL` env var) points there.

## Deploy

Automated by `.github/workflows/deploy-signal.yml`; both targets are
gated on the unit tests plus the `wrangler dev --local` protocol tests:

- **Production** (`newtonia-signal` — the baked-in default endpoint in
  `net_signal.cpp`): deploys on every `v*.*.*` release tag, the same
  trigger as the other production deploys, or via manual dispatch with
  target `production`. CI runs the exact same plain `npx wrangler
  deploy` the manual flow always did (production is the top-level
  wrangler config, not a named env), so runtime secrets and Durable
  Object state carry over untouched.
- **Beta** (`newtonia-signal-beta`, the `[env.beta]` in wrangler.toml):
  auto-deploys on every master push that touches `signal/`, or via
  manual dispatch (the default target). A fully separate Worker — own
  Durable Object namespaces, own secrets, own URL — so testing never
  disturbs live rooms. Point any build at it:

  ```sh
  NEWTONIA_SIGNAL_URL=wss://newtonia-signal-beta.gfmcc.workers.dev/ws ./newtonia
  ```

  (or set `signal_url` in the preferences INI). A fresh beta worker has
  NO secrets: TURN stays STUN-only until `TURN_KEY_ID`/`TURN_API_TOKEN`
  are set on the env, and the origin allowlist is the built-in default —
  browser testing from a non-shipped origin needs `ALLOWED_ORIGINS`.
  Manage beta secrets with `npx wrangler secret put NAME --env beta`
  (all the secrets/kill switches below take `--env beta` the same way).

Workflow credentials (GitHub repo secrets): `CLOUDFLARE_API_TOKEN`
(custom token with permission "Workers Scripts: Edit" — the dashboard's
"Edit Cloudflare Workers" template works) and `CLOUDFLARE_ACCOUNT_ID`
(`npx wrangler whoami`).

Manual deploy still works when needed:

```sh
cd signal
npx wrangler login              # once
npx wrangler deploy             # production
npx wrangler deploy --env beta  # beta
```

### Beta TURN setup

There is no separate TURN server to run — TURN is Cloudflare Realtime
(Calls), and the worker mints short-lived credentials itself whenever
`TURN_KEY_ID` + `TURN_API_TOKEN` are present on its env (see
`turn_ice_servers` in `worker.js`). A fresh beta worker has neither, so
it stays STUN-only until you attach a key. To set beta up like
production:

```sh
cd signal
# The Cloudflare Realtime TURN key (dashboard -> Realtime -> TURN, or the
# Calls API POST /accounts/<id>/calls/turn_keys): key ID + its API token.
npx wrangler secret put TURN_KEY_ID    --env beta
npx wrangler secret put TURN_API_TOKEN --env beta
```

That's enough to enable minting — a bad pair silently yields `[]`
(STUN-only), so verify success by connecting a host and checking for
`turn:`/`turns:` ICE frames ahead of the room code (or `npx wrangler
tail --env beta` -> `turn creds minted, ttl=…s`).

**Beta credentials, prod bandwidth.** A dedicated beta TURN key isolates
the *credentials* (roll or revoke beta's without touching prod), but
relay egress bills to the one Cloudflare account — beta and prod share
the same Realtime free tier (~1,000 GB/month). Test traffic counts
against the same pool.

**Optional budget cap on beta.** Prod's automatic TURN budget only gates
prod minting; the beta worker checks its own env, so without these it
mints with no auto-cutoff. To make beta pause too, set all three (any
one missing = the budget can't be measured and minting stays open):

```sh
npx wrangler secret put CF_ANALYTICS_TOKEN --env beta  # "Account Analytics: Read" token
npx wrangler secret put CF_ACCOUNT_ID      --env beta  # account tag (npx wrangler whoami)
npx wrangler secret put TURN_BUDGET_GB     --env beta  # decimal GB, e.g. 100
```

`TURN_BUDGET_GB` is compared against the **account-wide** month-to-date
egress, not beta's slice — `100` means "beta drops to STUN-only once the
whole account crosses 100 GB this month," which layers under prod's 900
so beta yields first. Defaults to 900 if unset. All beta secrets persist
across deploys (Cloudflare stores them per script), so this is one-time.

## Endpoints

- `GET /ws?role=host` — WebSocket; server replies `{t:"room",code:"ABCD"}`
- `GET /ws?role=join&code=ABCD` — WebSocket; `{t:"joined"}` or `{t:"err",...}`
- Host sends `{t:"offer",sdp}`; joiner sends `{t:"answer",sdp}`; the room
  relays each to the other side and replays the offer to late (re)joiners.
- Each side sends `{t:"identity",platform,name,cred?}` (peer identity —
  NETPLAY.md V0/V1/V2). The worker verifies `cred` against the claimed
  platform's backend — Steam Web-API ticket (`steam_verify.js`) or Play Games
  server auth code (`play_games_verify.js`) — and broadcasts
  `{t:"identity",role,platform,name,verified}` to the PEER, storing it for
  replay to a late joiner / reclaimed host. Verification never gates the room —
  a failure just leaves `verified` false (the game renders a role label).

## Peer-identity verification (NETPLAY.md V0/V1/V2)

> **Shared with the board worker via Cloudflare Secrets Store.** The three
> verify secrets below (`STEAM_WEBAPI_KEY`, `PLAY_GAMES_OAUTH_CLIENT_ID`,
> `PLAY_GAMES_OAUTH_CLIENT_SECRET`) are the same values the leaderboard
> worker needs, so they now live in ONE account-level store that both
> workers bind (`[[secrets_store_secrets]]` here and in `board/` — the
> store id `68f959729bdb4c3aa7793c420e740d97` is committed in both
> wrangler.tomls; store + secret creation per the **board/README.md**
> runbook). The `wrangler secret put` commands below are
> the legacy per-worker path (still works — `read_secret()` in
> `src/secret.js` reads either shape — but once the store is bound, delete
> the per-worker copies so there's a single source). TURN and other
> signal-only secrets stay per-worker.

Each platform verifier attests a claimed identity by proving the account with
the platform's own backend and deriving the display name server-side (a lying
wire `name` stops mattering). Verification is display-only and NEVER gates the
room — a missing secret, or any failure, leaves the peer role-labelled (see
NETPLAY.md's architectural backstop). Credentials travel client→worker over
wss only; nothing account-shaped ever goes peer-to-peer.

**Steam (V1)** needs a **publisher Web-API key** (from a key group scoped to
app 4536720). Optional `STEAM_IDENTITY` overrides the identity string the
ticket is bound to (must match the client's `WEBAPI_IDENTITY`, default
`newtonia-signal`).

```sh
npx wrangler secret put STEAM_WEBAPI_KEY           # production
npx wrangler secret put STEAM_WEBAPI_KEY --env beta # beta worker
```

**Play Games / Android (V2)** needs the game's OAuth 2.0 **web** client id +
secret (Google Cloud console → Credentials, under the Play Games project). The
client mints a single-use server auth code
(`GamesSignInClient.requestServerSideAccess`, `PlayGamesIdentity.java`); the
worker redeems it at Google's token endpoint and reads the verified player from
`games/v1/players/me`. The matching web client id also goes into the app
(`play_games_oauth_client_id` in `res/values/games-ids.xml`).

```sh
npx wrangler secret put PLAY_GAMES_OAUTH_CLIENT_ID       # production
npx wrangler secret put PLAY_GAMES_OAUTH_CLIENT_SECRET
npx wrangler secret put PLAY_GAMES_OAUTH_CLIENT_ID     --env beta
npx wrangler secret put PLAY_GAMES_OAUTH_CLIENT_SECRET --env beta
```

For local e2e (`test/e2e/identity_attested.sh`, `test/identity_test.js`) the
`FAKE_VERIFY` **dev var** attests the claim without contacting any platform
backend — pass `--var FAKE_VERIFY:1` to `wrangler dev`. **Never** set it in
production. Unit coverage of the verifiers is mocked (no account involved):
`node test/steam_verify_test.mjs`, `node test/play_games_verify_test.mjs`.

## Cost / abuse notes

The paid resources are the Worker/DO invocations and TURN relay bandwidth
(Cloudflare Calls). Defenses in `worker.js`:

- **Per-IP rate limits** (`Limiter` DO, fixed 10-min window): 10 host /
  30 join attempts. Keyed on the IP collapsed to its **/64** for IPv6
  (`rate_key`) so a v6 user can't rotate host bits to bypass the cap. The
  window counters are **persisted to DO storage**, so they survive the
  eviction of an idle Limiter DO (an in-memory counter would silently reset
  and leak the cap); each Limiter self-cleans via an alarm one window later.
- **Origin allowlist**: browsers always send (and can't forge) an `Origin`
  header on the WebSocket handshake, so only the shipped web origins
  (`newtonia.metonymous.com`, the itch.io game iframe) may open a socket —
  an unrelated page a player visits can't open `role=host` in their browser
  and harvest the TURN credentials. Non-browser callers (the native client)
  send no `Origin` and are allowed (already bound by the per-IP caps).
  Override with the `ALLOWED_ORIGINS` secret (comma-separated; a leading dot
  matches subdomains).
- **TURN credentials** are minted only on a rate-limited host attempt, a
  join to an actually-open room, or a **token-validated** host reclaim (the
  room confirms the reclaim token before a credential is minted, so a
  wrong-token reclaim costs nothing). Default **4-hour TTL** — TURN
  allocations must outlive the whole session (they can't be renewed
  mid-game: libdatachannel has no ICE restart), and long co-op sittings are
  expected, so the credential is deliberately long-lived. The
  harvested-credential risk this creates is bounded by the per-IP mint caps
  and the origin allowlist above, not by a short TTL. Override (seconds,
  clamped to Cloudflare's 48 h cap) with the `TURN_TTL` secret.
- **Relay content is length-capped** (SDP 16 KB, candidate 512 B, ≤32
  buffered) so a peer can't pin unbounded DO memory.
- **WebSocket Hibernation API**: after the handshake the signaling socket
  is idle for the whole game session (traffic is peer-to-peer), so the DO
  evicts between messages and stops billing wall-clock for held sockets.
  Room state lives in DO storage; sockets recover by tag. A storage alarm
  frees abandoned rooms after the grace window.

There is **no hard monthly spend cap** on paid Workers — the backstops are:
the Free-plan platform cap of 10 ms CPU per invocation (a per-script
`[limits] cpu_ms` is paid-only and is intentionally omitted from
wrangler.toml — see the note there), **Billing → Notifications** usage
alerts in the dashboard, and the manual kill switches below.

### Kill switches (CLI, no redeploy)

Two runtime flags, toggled with `wrangler secret` — they take effect on the
next request, and `secret delete` restores service. No `wrangler deploy`
needed.

```sh
# Master off — refuse all rooms. Hosts drop to the manual clipboard code
# flow (fall_back_to_manual), so friends can still play; no TURN is minted.
npx wrangler secret put DISABLED     # value is ignored; type anything
npx wrangler secret delete DISABLED  # back online

# TURN off — cut the metered relay bandwidth (the per-GB cost); signaling
# keeps working and the game silently continues STUN-only.
npx wrangler secret put TURN_OFF
npx wrangler secret delete TURN_OFF
```

## Automatic TURN budget (the free-tier hard cap)

The worker also trips TURN_OFF's behaviour AUTOMATICALLY when the account's
real month-to-date TURN egress (read from the GraphQL Analytics API,
`callsTurnUsageAdaptiveGroups`, cached ~15 min) crosses a budget — default
**900 GB**, 90% of the Realtime free tier's 1,000 GB/month. Past the cap
minting pauses (STUN-only; direct pairs unaffected) until the UTC month
rolls over. Log line on `wrangler tail`: `turn budget tripped`.

Setup (without these the budget can't be measured and minting stays open —
it is a cost cap, not an auth gate; the per-IP mint limits still apply):

```sh
# API token: dashboard -> My Profile -> API Tokens -> Create Token ->
# custom, permission "Account Analytics: Read" on this account.
npx wrangler secret put CF_ANALYTICS_TOKEN
# The account tag (dashboard URL /<hex id>/ or `npx wrangler whoami`).
npx wrangler secret put CF_ACCOUNT_ID
# Optional override, in GB (e.g. while testing: 1):
npx wrangler secret put TURN_BUDGET_GB
npx wrangler deploy
```

Unit tests: `node test/turn_budget_test.mjs` (mocked GraphQL; covers the
trip, the cache window, API-failure verdict-holding, and the month query).

Bluntest option, if you'd rather remove the worker entirely: `npx wrangler
delete` (clients hit the 12 s signal timeout, then fall back to manual
codes). The Calls TURN key can also be rolled/deleted in the dashboard,
which revokes every outstanding credential instantly.

## Tests

- `node test/reclaim_test.js` — protocol test (needs `wrangler dev` on :8787).
- `node test/rate_key_test.mjs` — `rate_key` /64-collapse unit test.
- `node test/origin_test.mjs` — `origin_allowed` allowlist unit test.
- `node test/pv_replay_test.mjs` — stored-offer `pv` replay test (needs
  `wrangler dev` on :8787).
- `node test/steam_verify_test.mjs` — Steam identity verifier, mocked Valve.
- `node test/play_games_verify_test.mjs` — Play Games identity verifier,
  mocked Google (token exchange + `players/me`).
- `node test/identity_test.js` — identity attest/broadcast/replay protocol
  test (needs `wrangler dev` on :8787 with `--var FAKE_VERIFY:1`).
- The PB-D5 multi-join families (all need `wrangler dev` on :8787; the
  identity ones need `FAKE_VERIFY`; the full suite in one window needs the
  `--var RATE_HOST_LIMIT:200 --var RATE_JOIN_LIMIT:500` limiter headroom —
  see deploy-signal.yml): `capacity_test.mjs` (cap at 3 + slot reopen +
  never-reused ids), `relay_isolation_test.mjs` (addressed offers/cands,
  `from` stamps), `jid_buffer_test.mjs` (legacy one-shot replay consumed,
  dropped-jid buffers die), `identity_fanout_test.mjs` (fan-out + per-jid
  late-verify), `grace_broadcast_test.mjs` (host-lost/host-back to all),
  `host_close_broadcast_test.mjs`, `offer_pv_test.mjs` (per-offer pv).
  Shared harness: `test/ws_harness.mjs`.
