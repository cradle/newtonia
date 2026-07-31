# Newtonia leaderboard worker

The Cloudflare Worker behind the online leaderboard (LEADERBOARD.md):
seasonal score table in D1, replay blobs in R2, one WebSocket endpoint at
`/board` speaking JSON control frames + binary chunks (protocol at the top
of `src/worker.js`). Admission requires platform attestation — the verify
modules are imported from `../signal/src/` so signaling and leaderboard
share one implementation.

## Layout

- `src/worker.js` — fetch router, per-connection `Session` DO, per-IP
  `Limiter` DO, retention cron (`scheduled`)
- `src/validate.js` — pure `.nrp` header + record-framing validation
  (the game reader's checks, ported; unit-testable under plain node)
- `test/` — unit tests (plain node) + protocol test (wrangler dev)

## Tests

```sh
node test/validate_test.mjs        # header/framing validation
node test/identity_gate_test.mjs   # attestation admission gate
```

Protocol test against the real worker code under miniflare (no Cloudflare
account involved). `FAKE_VERIFY` attests claims without a platform
backend; `SUBMIT_LIMIT` widens the per-IP submit window for the test's
burst. Never set either in production.

```sh
npx wrangler@4 dev --local --port 8788 --var FAKE_VERIFY:1 --var SUBMIT_LIMIT:100
node test/board_test.mjs           # BOARD_TEST_URL overrides ws://127.0.0.1:8788/board
```

The retention cron can be fired locally with
`curl "http://127.0.0.1:8788/cdn-cgi/handler/scheduled"`.

## Resource setup — automated

`wrangler dev --local` simulates D1/R2 and needs nothing. For a REAL deploy
the D1 databases and R2 buckets are created automatically on first run and
the real `database_id` is resolved and injected into the config at deploy
time by `board/scripts/ensure-resources.sh` (invoked from
`deploy-board.yml`) — no Terraform and no hand-pasted id. The `database_id`
in `wrangler.toml` stays an obvious placeholder in git; only the deploy
checkout gets the real value substituted.

For the automation to create resources, `CLOUDFLARE_API_TOKEN` must include
**D1:Edit** and **Workers R2 Storage:Edit** (the dashboard's "Edit
Cloudflare Workers" template plus those two), not just Workers Scripts:Edit.

To run the bootstrap by hand (or provision without deploying):

```sh
cd board
CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... \
  bash scripts/ensure-resources.sh beta        # or: production
# prints DATABASE_ID=<uuid>; the deploy workflow injects it for you
```

The script is idempotent — it creates a database/bucket only when absent
and otherwise just resolves the existing id, so re-running is safe.

Secrets, per environment (`--env beta` for the beta worker): the same
platform verification secrets the signal worker uses —
`STEAM_WEBAPI_KEY`, `PLAY_GAMES_OAUTH_CLIENT_ID`,
`PLAY_GAMES_OAUTH_CLIENT_SECRET` (Game Center needs none). Optional:
`ALLOWED_ORIGINS` (browser origins, v1 default is native + local dev
only), `DISABLED` (kill switch, any value).

The schema is created on demand (`CREATE TABLE IF NOT EXISTS` on first
query) — no migration step.

## Deploy

`deploy-board.yml`: `v*.*.*` tags deploy production (`newtonia-board`),
master pushes touching `board/**` or the shared verify modules deploy the
isolated beta worker (`newtonia-board-beta`); manual dispatch picks
either. Point a build at beta with
`NEWTONIA_BOARD_URL=wss://newtonia-board-beta.gfmcc.workers.dev/board`.

## Knobs

- `KEEP_N` (100): rows at rank <= KEEP_N keep their replay blob; the
  daily cron demotes the rest to score-only.
- `SCORE_ONLY_AFTER_MS` (180 days): seasons with no newer submission
  lose all blobs.
- Rate limits: 60 connects / IP / 10 min, 6 submits / IP / hour, plus
  per-connection budgets (120 queries, 2 submits, 5 fetches).
