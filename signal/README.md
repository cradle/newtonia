# Newtonia signaling worker

Cloudflare Worker + Durable Objects that mints 4-letter room codes and
relays the WebRTC offer/answer between host and joiner. Protocol and
milestone context: `../NETPLAY.md` (Milestone 2).

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

## Endpoints

- `GET /ws?role=host` — WebSocket; server replies `{t:"room",code:"ABCD"}`
- `GET /ws?role=join&code=ABCD` — WebSocket; `{t:"joined"}` or `{t:"err",...}`
- Host sends `{t:"offer",sdp}`; joiner sends `{t:"answer",sdp}`; the room
  relays each to the other side and replays the offer to late (re)joiners.

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
