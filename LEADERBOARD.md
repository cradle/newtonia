# Leaderboard — Design & Implementation Plan

Planning doc for the online leaderboard (REPLAY.md's R4, given its own home).
Style mirrors NETPLAY.md/REPLAY.md: decisions first, then phased milestones
with exit criteria. Nothing here is built yet.

What the replay work already hands this project, so nothing below rebuilds
it: `best.nrp` as the submission candidate (promotion already requires
`FLAG_CLEAN` and excludes `FLAG_CHEATED`); a header carrying score,
generation, duration, date, player count, `run_id`, `save_version` and —
populated since 2026-07-30 — the game version string that is the season key;
R2 playback of an arbitrary file (the "download → watch" half); and the
ingest hardening that treats a stranger's `.nrp` as hostile input
(bounded counts, screened floats, `MAX_RECORD_SLOT`, the web file cap).

## Decisions (proposed — confirm with Glenn before L1)

- **Ask, don't auto-upload — and only ask when the run would place.** The
  trigger is a two-stage gate at game over:
  1. *Local gate*: the run finalized with `FLAG_CLEAN`, without
     `FLAG_CHEATED`, and its score beat the previous `best.nrp` header score
     (i.e. the best-promotion check passed — a new personal best). No
     network traffic for ordinary runs.
  2. *Remote gate*: an async `qualify` query to the worker — "would this
     score place on the current season's board?" The worker answers with
     the projected rank and the current cut-line (lowest charting score).
     Only a would-place answer shows the prompt; a worker that is
     unreachable or slow shows nothing (the offer stays available later —
     see the REPLAYS-menu retry path). This is the "query the worker for
     the lowest score" flow: the query is one D1 row read, so it is cheap
     enough to fire on every personal best, and it means the player is
     never asked to upload megabytes that would bounce off the bottom of
     the table.
  The prompt is "UPLOAD TO LEADERBOARD?" with the projected rank shown
  ("WOULD PLACE #7 THIS SEASON"), YES/NO through the shared confirm
  grammar (`MenuSelect`, stacked on desktop/controller, half-split on
  touch). Default highlight NO, matching the "New game?" confirm — upload
  publishes data, so the accident-proof default is the quiet one.
- **The blob IS the submission.** The upload body is the finalized
  `best.nrp` bytes; the worker parses the 64-byte header server-side and
  takes score/season/duration/player-count from the FILE, never from a
  separately claimed field — there is nothing for a dishonest client to
  disagree with. The `qualify` stage sends bare numbers (no blob), but its
  answer is advisory only; admission is re-checked at submit time against
  the header the worker parsed itself.
- **Transport: the signaling WebSocket stack, not a new HTTP client.** The
  game has no HTTPS client on native (libdatachannel provides WebSocket
  only; that is what `NetSignal` uses on every platform, browser WS on
  web) and adding libcurl/platform-HTTP for one feature is a dependency
  and a per-platform matrix we don't need. The leaderboard client is a
  sibling seam to `NetSignal` — `NetBoard` (`net_board.h/cpp`), same
  backend split (`net_board_rtc.cpp` / `net_board_web.cpp`), same
  JSON-frame protocol style — speaking WSS to the leaderboard worker.
  Uploads and replay downloads travel as binary WS frames in 64 KB chunks
  between JSON control frames. Wire cost is identical to HTTPS; code cost
  is near zero because both backends already exist in the signal seam to
  copy from.
- **Server: a second Worker on the same Cloudflare account (Free plan),
  `newtonia-board` in a new top-level `board/` dir** mirroring `signal/`
  (own `wrangler.toml`, own beta env, own tests). Separate script so a
  leaderboard deploy can never touch live signaling rooms; it imports the
  existing verify modules (`signal/src/steam_verify.js`,
  `play_games_verify.js`, `game_center_verify.js`) by relative path —
  wrangler bundles them — and registers its own copy of the `Limiter` DO
  class for per-IP rate limits. Bindings: one D1 database (`scores`), one
  R2 bucket (`newtonia-replays`). Free-tier fit (verified 2026-07):
  Workers 100K req/day, D1 5 GB / 5M row-reads/day, R2 10 GB-month with
  zero egress (replays get *downloaded*, so free egress is the feature
  that makes watchable rows viable), 100 MB request bodies. The retention
  policy below keeps storage in the tens of MB per season.
- **Identity rides the existing attestation.** The submit frame carries
  the same `{platform, name, cred}` triple `NetSignal::send_identity`
  sends, and the worker verifies it through the same per-platform modules
  (Steam ticket, Play Games auth code, Game Center signature — one shared
  throttle). Attested rows show the platform-verified name + badge;
  Game Center attests account-only, so an iOS row shows the badge with the
  claimed alias folded through `net_sanitize_name` but flagged unverified
  (same rule as the online peer display). **Unattested submissions are
  accepted** (plain desktop has no credential source) with the claimed
  name folded/capped by the same `net_sanitize_name` and rendered
  unverified; the Limiter bounds anonymous spam per IP. The platform
  account key (from attestation) is stored hashed, only for per-player
  dedup — never displayed, nothing persisted client-side (XR-014 posture).
- **Seasons = the header's `game_version` string, verbatim** (the 23-char
  season key REPLAY.md locked). The worker groups rows by exact string;
  the "current season" is simply the version the submitting build stamps.
  No server-side season table to maintain — a release that changes the
  stamp opens its season implicitly. Solo and co-op are **separate boards**
  (`player_count` 1 vs 2 — scores aren't comparable across them).
- **Dedup: one row per run, one row per player.** Primary key
  `(season, run_id)` — resubmitting the same run (e.g. after a clean
  abandon was uploaded, then resumed and improved) upserts if the score is
  higher. Additionally, an attested player keeps only their best row per
  season+board (`platform_key` unique index); anonymous rows skip that
  check (no stable key) and rely on the per-IP Limiter. `FLAG_ENDED` is
  deliberately NOT required: the clean-abandon → NEW-GAME promotion path
  produces a legitimate `best.nrp` without it, and the `run_id` upsert
  makes the resume-and-improve case converge on one honest row.
- **Size cap 32 MB** (the web recorder's existing cap, ~2 h of play);
  typical submissions are single-digit MB. Whole blob, never trimmed —
  trimming would break the one-continuous-run property the `run_id`
  resume-append machinery exists to preserve. The worker rejects larger
  at the control frame (before any chunk is accepted).
- **Retention: blobs for charting rows only.** Top `KEEP_N` (100) rows per
  season+board keep their replay in R2; a row demoted below the line has
  its blob deleted (the score row stays — REPLAY.md's "old seasons keep
  replays best-effort, eventually score-only"). A daily Cron Trigger
  sweeps; seasons older than `SCORE_ONLY_AFTER` (180 days) drop all blobs.
  Bounds R2 to `KEEP_N × seasons × ~5 MB` — far inside the free tier.
- **Verification field lives in the submit envelope, not the `.nrp`.** A
  reserved `verify` member of the submit JSON frame (absent in v1) is
  where R5's input-log proof slots in without a file-format break —
  REPLAY.md already established the fixed head has no spare bytes.
- **Downloaded replays are transient.** A leaderboard row's replay
  downloads to `replays/download.nrp`, plays via the existing
  `start_replay_playback` path, and is overwritten by the next download.
  It is NOT listed on the REPLAYS menu (it's someone else's run; the
  leaderboard screen is its home) and never best-checked.

## Server API — `board/src/worker.js` (WS at `/board`, JSON + binary frames)

```
client → {t:"qualify", season, score, players}
worker → {t:"qualify", place, cutline, would_place}   // one D1 read

client → {t:"top", season, players, count}            // count ≤ 100
worker → {t:"top", rows:[{rank, name, platform, verified, score,
                          generation, duration_ms, date, has_replay,
                          run_id}]}

client → {t:"submit", size, platform, name, cred}     // size ≤ 32 MB
worker → {t:"submit-ok"}                              // or {t:"error", reason}
client → binary chunks (64 KB) … {t:"submit-end"}
worker → validates, stores, {t:"placed", rank}        // or {t:"error"}

client → {t:"fetch", season, run_id}
worker → {t:"fetch-ok", size} + binary chunks + {t:"fetch-end"}
```

Submit-side validation, all before the D1 insert / R2 put: total size
matches the announced `size`; header magic/format-version/`header_size`
sane (the same checks `read_header_status` makes, ported); `FLAG_CLEAN`
set, `FLAG_CHEATED` clear; score > 0; a bounded walk of the record framing
— first record is a KEYFRAME, `[u32 slot | u8 kind | u32 len]` framing
self-consistent to EOF (truncated tail tolerated), slots monotonic
non-decreasing and ≤ `MAX_RECORD_SLOT`, at least one DELTA. The walk is
O(record count) with no payload parsing, well inside the 10 ms Free-plan
CPU budget for files this size; payload *contents* stay unvalidated (the
game's own hardened reader handles hostile payloads at watch time — that
work already landed). Rate limits via the Limiter DO: qualify/top cheap
and generous, submit strict (a handful per IP per hour), fetch moderate.

D1 schema (one table):

```
scores(season TEXT, players INTEGER, run_id TEXT, score INTEGER,
       generation INTEGER, duration_ms INTEGER, submitted_at INTEGER,
       name TEXT, platform INTEGER, verified INTEGER,
       platform_key TEXT,          -- hashed attested account id, '' if none
       blob_key TEXT,              -- R2 key, '' once demoted to score-only
       PRIMARY KEY (season, run_id))
CREATE INDEX scores_rank ON scores(season, players, score DESC);
```

## Client — `net_board.h/cpp` + UI

- `NetBoard` seam: `create()` per platform (rtc/web backends, null
  elsewhere — same factory shape as `NetSignal`), `qualify()`, `top()`,
  `submit(path, identity, cred)`, `fetch(season, run_id, dest_path)`,
  `poll(Event&)` on the main thread. Uploads stream the file in chunks so
  no whole-blob RAM copy is needed beyond the existing Reader buffer.
  URL: `NEWTONIA_BOARD_URL` env override, baked-in production default —
  the `net_signal_url()` pattern.
- **Game-over hook** (`GLGame`): after `Recorder::finalize` retires the
  run, if the best check promoted (the local gate — `rotate`/`best_check`
  need to surface a "promoted" result, a small `replay.h` addition), fire
  `qualify` and keep polling in the game-over card's tick. A would-place
  answer swaps the card's lower half to the UPLOAD prompt; YES starts the
  upload with a progress line ("UPLOADING… 43%", Esc cancels), then
  "UPLOADED — RANK #7" or "UPLOAD FAILED — TRY AGAIN FROM REPLAYS". The
  card must never block on the network: no answer within ~4 s = no prompt,
  and leaving to the menu abandons the query harmlessly. Identity
  credentials are minted when the query fires (async warm, like the lobby
  does at open).
- **Retry path**: the REPLAYS list's BEST RUN row gains an UPLOAD action
  (desktop: a second column action via the existing row grammar; touch: a
  long-row split like the lobby's Choose). Same qualify → prompt → upload
  flow, so a missed or declined game-over prompt is never final.
- **LEADERBOARD menu row** (below REPLAYS, hidden when the platform has no
  `NetBoard` backend or `net_online_play_allowed()` is false): a list
  screen fetching `top` on open (spinner row while pending, "UNAVAILABLE"
  on error/timeout), rows showing rank, name + platform badge (the
  attested-display rules), score, level, date; SOLO/CO-OP toggle on a
  header row. Selecting a `has_replay` row downloads with progress and
  hands `download.nrp` to `start_replay_playback`; rows without a blob are
  unselectable (score-only). Shared nav ladder + TapBand geometry
  throughout — no hand-rolled input.
- **Prompt fatigue valve**: an Options row "LEADERBOARD PROMPTS ON/OFF"
  (default ON). OFF suppresses the game-over prompt only; the REPLAYS
  UPLOAD action always works.

## Milestones

### L1 — worker
`board/` dir: wrangler.toml (prod + beta envs, D1/R2/Limiter bindings),
worker.js implementing the API above, verify modules imported from
`signal/src/`, header/record-walk validation ported from `replay.cpp`,
retention cron. Unit tests in `board/test/` (mocked D1/R2 where needed,
the signal suite's style) + a `wrangler dev --local` protocol test
driving qualify/submit/top/fetch end-to-end with a real recorded `.nrp`
fixture — including rejection cases (cheat flag, oversize, bad framing,
non-keyframe first record, rate limit).
**Exit**: protocol test green locally; beta worker deployed by hand;
`curl`-level smoke against beta documented in `board/README.md`.

### L2 — client seam + submit flow
`net_board.h/cpp` + both backends; the game-over qualify → prompt →
upload flow; the REPLAYS BEST RUN retry action; Options prompt toggle.
Headless e2e `test/e2e/leaderboard.sh`: runs `wrangler dev` locally,
records a run (`NEWTONIA_REPLAY_ENABLE`), drives game over, asserts the
prompt appears only on a qualifying personal best, uploads, asserts the
D1 row + R2 object exist and match the header, asserts a cheat-flagged
run never prompts and a worker-down run degrades silently.
**Exit**: e2e green headless on Linux CI (linux.yml gate, like the replay
drivers); field pass on one desktop + one Android device.

### L3 — leaderboard screen + watch
LEADERBOARD menu row, list screen, SOLO/CO-OP toggle, download →
playback via `download.nrp`. e2e extends: seed the local worker with two
rows, open the screen, select, assert playback reaches the world (the
replay_playback.sh technique).
**Exit**: keyboard/controller/touch all drive list → watch → back; a
score-only row is visibly unselectable; screenshots verified headless.

### L4 — deploy + season hygiene
`deploy-board.yml` mirroring deploy-signal.yml (tags → prod, master
pushes touching `board/**` or `signal/src/*_verify.js` → beta, gated on
the L1 tests); retention cron verified against a seeded past season;
`board/README.md` runbook (D1/R2 one-time creation, secrets, the
Limiter's knobs). Decide the public-web posture (see open questions)
before this ships.
**Exit**: a `v*.*.*` tag deploys both workers; beta isolation confirmed
(beta rows never appear in prod).

### L5 — deferred: verification
R5's input-log re-simulation, arriving through the reserved `verify`
envelope field. Nothing built until forged submissions actually appear;
the social deterrent (every row is watchable) is the v1 defense.

## Open questions

- **Prompt default YES or NO?** Written as NO above (matches the repo's
  confirm convention and the privacy posture); YES converts better.
  Glenn's call.
- **Public web build**: the Pages/itch `html5` deploys are netless by the
  NETPLAY.md pricing decision (`NEWTONIA_NET_DISABLED`), but the
  leaderboard is not co-op — does the free web game get the board
  (upload? view-only? neither)? View-only is a plausible middle: the
  board as an ad for the paid online build. Binds at L4.
- **Co-op attribution**: a 2P row credits the submitting account only
  (the header has no second identity). Both sides of an online run share
  a `run_id`, so the PK stops double rows; is "first submitter wins the
  credit" acceptable for v1?
- **Anonymous rows on or off at launch?** Accepted-but-unverified is the
  design above; if early spam outruns the Limiter, flipping to
  attested-only is a worker-side switch, no client change.
- **Show the player's own rank when off-board** (a `rank-of {score}`
  query on the leaderboard screen: "YOUR BEST: #214")? Cheap to add in
  L3, skippable.
