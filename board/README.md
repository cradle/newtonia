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

## One-time resource setup (per environment)

`wrangler dev --local` simulates D1/R2 and needs none of this. Before the
first real deploy:

```sh
npx wrangler d1 create newtonia-board          # paste database_id into wrangler.toml
npx wrangler r2 bucket create newtonia-replays
# beta (own resources, never mixed with production):
npx wrangler d1 create newtonia-board-beta     # paste into [env.beta]
npx wrangler r2 bucket create newtonia-replays-beta
```

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
