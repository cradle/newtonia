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
     `FLAG_CHEATED`, and the best-promotion check passed. **Best is
     SEASON-scoped** (decided with Glenn 2026-07-31): a clean scoring run
     whose season differs from the stored best's promotes regardless of
     score — a fresh season is a clean slate, and gating on the old
     season's high score would keep any lower (but board-qualifying) run
     from ever prompting. `best.nrp` is therefore "your best run of the
     season you last played"; the old season's uploaded replay lives on
     on its board. Within a season the gate stays score-beats-best. No
     network traffic for ordinary runs. **Best is PER-BOARD** (decided
     with Glenn 2026-08-01, from a field report): the worker keeps solo
     and co-op tables apart, so the local candidate does too —
     `best.nrp` (solo) and `best_coop.nrp` (co-op), routed by the
     header's `player_count` (`Replay::best_path_for`). With one shared
     slot a solo high score silently shadowed every later co-op run:
     the co-op score never beat it, so the run never promoted and the
     CO-OP board never got its upload prompt. `take_best_promoted()`
     now returns the promoted slot's PATH (empty = none) so the
     game-over qualify/upload operate on the right file; the menu
     screen loads the browsed board's own slot on every SOLO/CO-OP flip
     (upload row, rank-of footer, `- YOU` tag all follow), and a
     pre-split co-op `best.nrp` (the old code promoted co-op runs into
     the shared slot when they DID beat the solo score) migrates to its
     slot lazily (`ensure_best_split`, ahead of every promotion check
     and `best_path_for` resolution). The REPLAYS list shows the new
     slot as BEST CO-OP; `NEWTONIA_REPLAY_PLAY=bestcoop` plays it.
     **Online co-op entries are PERSONAL claims** (decided with Glenn
     2026-08-02): each side's recording stamps its OWN pilot's score
     (`GLGame::replay_finish` — offline 2P keeps the best ship's score,
     one account claiming the run), and the CLIENT records under a
     derived run_id (bitwise NOT of the shared one — deterministic, so
     rejoin/resume matching still works), so both players' submissions
     are distinct rows on the CO-OP board under their own accounts and
     scores. No shared rows, no claim flow: a carried partner charts
     lower than their carrier, which is what a high-score table should
     say. The worker cleanly refuses a same-run_id submit from a
     DIFFERENT account ("already-submitted" — copied files, or a
     pre-split peer recording; it used to die on the primary key as
     "internal"). Verified headless: host+client loopback, complement
     run_ids, host stamps its own spray score, idle client stamps 0.
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
  touch). **Default highlight YES** (decided with Glenn 2026-07-31): the
  prompt only appears for a run that qualifies, so confirming is the
  expected action, and the two-stage gate already keeps it rare. This
  deliberately differs from the "New game?" confirm's NO default — that
  one guards against destroying a save; declining an upload loses
  nothing (the REPLAYS retry path remains).
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
  backend split, same JSON-frame protocol style — speaking WSS to the
  leaderboard worker. **v1 ships the native backend only**
  (`net_board_rtc.cpp`); see the no-web decision below.
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
  Game Center attests account-only, so an iOS row carries the claimed
  alias folded through `net_sanitize_name`, flagged unverified — and the
  board DISPLAYS it anyway (decided with Glenn 2026-08-01): the account
  is attested for admission, the alias is sanitized, and the stakes are
  display-only — the gamertag rule. This is deliberately looser than the
  online-peer identity display, where a lobby stranger's claim still
  never renders. **Submissions REQUIRE attestation**
  (decided with Glenn 2026-07-31): a submit whose credential is absent or
  fails verification is rejected (`{t:"error", reason:"unverified"}`) —
  every row on the board is tied to a real platform account, which is the
  spam/forgery bound the Limiter alone can't give. Consequences: plain
  non-Steam desktop builds have no credential source and cannot submit —
  the client gates the game-over prompt and the REPLAYS UPLOAD action on
  a present verify backend (`local_verify_credential` non-null), so those
  builds see a view-only leaderboard rather than a doomed upload; and the
  worker's verify modules become load-bearing for admission, not just for
  names (their existing failure mode — attest nothing — hardens to
  reject). Viewing (`qualify`/`top`/`fetch`) stays open to everyone. The
  platform account key is stored hashed, only for per-player dedup —
  never displayed, nothing persisted client-side (XR-014 posture).
- **Seasons = the header's `game_version` string, verbatim** (the 23-char
  season key REPLAY.md locked). The worker groups rows by exact string;
  the "current season" is simply the string the submitting build stamps.
  No server-side season table to maintain — a release that changes the
  stamp opens its season implicitly. Solo and co-op are **separate boards**
  (`player_count` 1 vs 2 — scores aren't comparable across them).
- **Production admits canonical seasons only** (decided with Glenn
  2026-08-01): the production worker sets `CANONICAL_SEASONS_ONLY="1"`
  (top-level wrangler.toml — production config is the default truth) and
  refuses submissions whose season is not a SEASON-file stamp (`s<N>`),
  answering `bad-season` ("SEASON NOT ACCEPTED" on the cards); `qualify`
  answers would_place=false for them, so a dev build pointed at
  production simply never prompts. Beta sets no vars and stays open for
  dev/test submissions. The browser lists canonical seasons only on BOTH
  (a build browsing its own non-listed season still can — the client
  seeds its own and best.nrp's seasons locally). Covered by
  `board/test/whitelist_test.mjs` (production default) and the
  `CANONICAL_SEASONS_ONLY:0` opt-out the open-mode harnesses pass.
- **The stamp is the root `SEASON` file, bumped deliberately** (decided
  with Glenn 2026-07-31; starts at `s1`): seasons rotate when the game
  changes significantly, not on release cadence — at v1.48.x per-tag or
  per-minor buckets would reset boards nobody wanted reset, and per-major
  would never reset at all. All four deploy workflows stamp the checked-out
  `SEASON` content on **tag builds only**; manual dispatches keep their
  honest non-release buckets (branch name; iOS beta-worker keeps its
  v<maj.min>.9xx staging sentinel, Android dispatch its 0.0.1-sha) and dev
  builds keep `git describe`, so ad-hoc builds never chart on the live
  board. The bump test: would the change make older replays play back
  wrong, or older scores unfair to compete against? Then bump — a one-line
  PR editing `SEASON` (single line, ≤23 chars, whitespace-stripped on
  read). Because a season now spans builds, in-season replay playback
  across patch releases is expected — another reason the bump rule tracks
  gameplay-affecting changes.
- **Dedup: one row per run, one row per player.** Primary key
  `(season, run_id)` — resubmitting the same run (e.g. after a clean
  abandon was uploaded, then resumed and improved) upserts if the score is
  higher. Additionally, each player keeps only their best row per
  season+board (`platform_key` unique index — every row has one now that
  attestation is required for admission).
- **Co-op credit goes to the submitter** (decided with Glenn 2026-07-31):
  a 2P row is admitted on the submitting player's attestation alone and
  displays their name; the partner is unattested and uncredited (the
  header carries a player count but no second identity, and requiring
  two credentials would block every co-op run whose partner has left).
  Both sides of an online run share a `run_id`, so the PK stops double
  rows — first submitter wins the credit, and the upsert-if-higher only
  ever replaces a row with the same run's better/final score. `FLAG_ENDED` is
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
  sweeps; a season with no submission in `SCORE_ONLY_AFTER` (180 days)
  drops all blobs — **except the live season** (the one with the newest
  submission, per players count), which is never dormancy-stripped no
  matter how long the game goes quiet (decided with Glenn 2026-07-31: the
  board players are actually browsing keeps its replays watchable; the
  strip reclaims R2 from seasons a release left behind, not the current
  one). The live season still gets the below-`KEEP_N` trim. Covered by
  `board/test/retention_test.mjs` (drives the real cron handler against
  in-memory D1/R2 fakes). Bounds R2 to `KEEP_N × seasons × ~5 MB` — far
  inside the free tier.
- **Verification field lives in the submit envelope, not the `.nrp`.** A
  reserved `verify` member of the submit JSON frame (absent in v1) is
  where R5's input-log proof slots in without a file-format break —
  REPLAY.md already established the fixed head has no spare bytes.
- **No leaderboard on web in the first release** (decided with Glenn
  2026-07-31) — all web builds, the netless public deploys and the paid
  itch `newtonia-online` alike. (The read-only **site** leaderboard page
  + watch deep link added 2026-08-04 — see "Site leaderboard" below —
  does not reopen this: it is the website rendering public board data,
  not the in-game feature, and it cannot submit.) `NetBoard::create()` returns null under
  `__EMSCRIPTEN__`, which the existing gates turn into a fully absent
  feature (no LEADERBOARD menu row, no game-over prompt, no REPLAYS
  UPLOAD action) with no per-screen web special-casing. Nothing is
  burned for later: web recording already produces a valid `best.nrp`,
  submissions couldn't pass admission there anyway until a web
  attestation story exists (no Steam/Play Games/Game Center credential
  in a browser), and enabling later is a `net_board_web.cpp` backend
  plus the factory — the UI lights up by itself.
- **The leaderboard screen shows the player's own rank, on-board or off**
  (decided with Glenn 2026-07-31). Opening the screen sends the player's
  local best score (read from `best.nrp`'s header — nothing uploads)
  in a `rank-of` query alongside the `top` fetch, and a footer line
  renders the answer: `YOUR BEST: #214`. A player whose row is on the
  board gets their row highlighted instead; a player with no recorded
  best gets no footer. This is a read, not a submission, so the
  attestation requirement doesn't apply — but a cheat-flagged best is
  excluded client-side (it could never have been submitted, so ranking
  it against the board would be a lie). Server cost is one D1 count on
  the index the rank listing already uses; client cost is one more JSON
  frame on the already-open socket — it is the `qualify` computation
  reused for display. The motivation is the off-board majority: a
  top-100 most players will never touch reads as someone else's game,
  while "#214" gives everyone a ladder to climb.
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
                          run_id, format, save_format}]}
                        // format/save_format: the replay's format_version
                        // + embedded savegame version, so a client can
                        // grey out rows its build cannot play back BEFORE
                        // downloading (0 = row predates the columns)

client → {t:"seasons"}                                // season browser
worker → {t:"seasons", rows:[{season, newest, count}]}// newest-first, ≤ 50

client → {t:"rank-of", season, players, score}        // display only —
worker → {t:"rank-of", place}                         // one D1 count on
                                                      // the rank index

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
       platform_key TEXT,          -- hashed attested account id (required)
       blob_key TEXT,              -- R2 key, '' once demoted to score-only
       PRIMARY KEY (season, run_id))
CREATE INDEX scores_rank ON scores(season, players, score DESC);
```

## Site leaderboard — the website's read-only view (2026-08-04)

The GitHub Pages site gets its own leaderboard URL,
`https://newtonia.metonymous.com/leaderboard/`, with in-browser replay
playback — WITHOUT changing the no-web-client decision: the game's
in-game leaderboard stays absent on web builds, and nothing here can
submit (admission is unchanged, WS + platform attestation only).

- **Daily task = the worker's existing retention cron.** After the
  retention/orphan passes, `scheduled()` builds a snapshot of every
  canonical season's top `KEEP_N` rows (both boards, live board per
  players count flagged) and writes it to R2 at `site/leaderboard.json`
  (a reserved prefix `sweep_orphans` skips). One extra publish per day,
  zero marginal D1 load from page views.
- **Two public read-only HTTP endpoints** on the board worker, CORS `*`
  (the data is what the WS protocol already serves any native client):
  `GET /site/leaderboard.json` (serves the snapshot; a cold miss builds
  and stores it, so a fresh deploy never 404s) and
  `GET /replay/<season>/<run_id>.nrp` (the blob download the WS `fetch`
  flow serves, same row/blob admission checks). Both ride the existing
  per-IP `Limiter` (`query` / `fetch` actions) and the `DISABLED` kill
  switch; the WS origin gate is untouched.
- **The page** (`web/site/leaderboard/`, copied by the `make web` cp
  list, deployed by the ordinary Pages build) renders the snapshot:
  SOLO/CO-OP toggle, season browser, platform badges + verified ticks,
  `?players=`/`?season=` deep links, `?board=beta` for the beta worker.
  Worker-supplied names render via `textContent` only. Data is at most a
  day stale by design (plus a 1 h `Cache-Control`) — the page says
  "standings refresh daily".
- **WATCH → in-browser playback.** Replay-bearing rows link
  `/play/?replay=<season>/<run_id>`: `web/main.ts` downloads the blob
  from `/replay/` and hands the bytes to `web_watch_replay`
  (`web_main.cpp`), which writes `Replay::download_path()`, pre-flights
  the header (a build that can't read the file alerts instead of
  silently no-oping), and stages a one-shot flag `Menu::tick` polls into
  the same `GLGame::start_replay_playback` path the in-game
  leaderboard's downloads use. The Pages build tracks master, so the
  live season's replays are playable there; old-season rows another
  format can refuse at the pre-flight.
- Tests: `board/test/site_test.mjs` (endpoints + snapshot staleness/cron
  refresh, driven through wrangler's `--test-scheduled` hook) and the
  snapshot-publish/orphan-exemption cases in `retention_test.mjs` — both
  gate `deploy-board.yml`.

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
  card must never block on the network: no answer within the qualify
  deadline (15 s — generous because a phone's first connection after a
  radio wake is slow, and a drop mid-qualify gets one silent reconnect;
  field-tested as "no prompt on the first game, fine on the second" at
  the original 4 s) = no prompt, and leaving to the menu abandons the
  query harmlessly. Identity
  credentials are minted when the query fires (async warm, like the lobby
  does at open).
- **Retry path**: an UPLOAD BEST RUN action row on the LEADERBOARD screen
  (revised at L3 from the planned REPLAYS BEST-RUN-row action: one
  leaderboard home beats overloading the replays list's row grammar with
  a second per-row action, and the screen already holds the socket). A
  missed or declined game-over prompt is never final.
- **LEADERBOARD menu row** (below REPLAYS, hidden when the platform has no
  `NetBoard` backend or `net_online_play_allowed()` is false): a list
  screen fetching `top` on open (spinner row while pending, "UNAVAILABLE"
  on error/timeout), rows showing rank, name + platform badge (the
  attested-display rules), score, level, date; SOLO/CO-OP toggle on a
  header row. Touch draws each entry over TWO lines (rank/name/score,
  then badge/level/date beneath — portrait has no width for the
  desktop's six columns; revised 2026-08-03), and every screen's
  vertical anchors hang off the portrait-aware half-height (half the
  portrait surplus, exactly the classic layout in landscape). Selecting a `has_replay` row downloads with progress and
  hands `download.nrp` to `start_replay_playback`; rows without a blob are
  unselectable (score-only). Shared nav ladder + TapBand geometry
  throughout — no hand-rolled input.
- **Prompt fatigue valve**: an Options row "LEADERBOARD UPLOAD ASK/AUTO"
  (`leaderboard_prompts`, default ASK). Revised 2026-08-03 from the
  original ON/OFF suppress-the-prompt design after a field tester read
  OFF as "no upload" when a prompt went missing: the setting picks how a
  qualifying would-place best is handled at game over — ASK shows the
  UPLOAD TO LEADERBOARD? prompt (the per-run opt-out), AUTO skips the
  question and uploads immediately, showing the same UPLOADING /
  UPLOADED - RANK #N status text. It never means "don't upload"; the
  LEADERBOARD screen's explicit upload action always works too.

## Milestones

### L1 — worker ✅ (landed on this branch, 2026-07-31)
Implementation notes, where reality refined the sketch: the WS terminates
in a per-connection `Session` DO (chunk reassembly under DO CPU budgets,
not the stateless worker's 10 ms), with in-DO per-connection message
budgets (120 queries / 2 submits / 5 fetches) so ordinary traffic costs
no Limiter round-trip per frame; the per-IP `Limiter` counts connects and
submits only. `SUBMIT_LIMIT` joined `FAKE_VERIFY` as a dev-only var (the
protocol test's submit burst trips the production 6/hour window from one
IP). workerd rejects non-function exports from the entry module, so the
tuning constants stay unexported. Tests: 59 unit checks (validation +
admission gate, plain node — `validate.js` is pure for exactly this) and
a 21-check protocol test against `wrangler dev --local`, including the
fetch round-trip asserting byte-identical replay bytes.

`board/` dir: wrangler.toml (prod + beta envs, D1/R2/Limiter bindings),
worker.js implementing the API above, verify modules imported from
`signal/src/`, header/record-walk validation ported from `replay.cpp`,
retention cron. Unit tests in `board/test/` (mocked D1/R2 where needed,
the signal suite's style) + a `wrangler dev --local` protocol test
driving qualify/submit/top/fetch end-to-end with a real recorded `.nrp`
fixture — including rejection cases (cheat flag, oversize, bad framing,
non-keyframe first record, rate limit, and a missing or failed-verify
credential — attestation is an admission requirement; a `FAKE_VERIFY`
dev var attests without contacting the platforms, the signal worker's
existing e2e pattern).
**Exit**: protocol test green locally; beta worker deployed by hand;
`curl`-level smoke against beta documented in `board/README.md`.

### L2 — client seam + submit flow ✅ (landed on this branch, 2026-07-31)
Implementation notes: `NetBoard` (net_board.h/cpp) ships the rtc backend
only per the no-web decision; the shared layer owns frame build/parse
(including the top-rows array scanner) so a later web backend is socket
code only. The trigger is `Replay::take_best_promoted()` — a one-shot
set by every best check — consumed at the game-over latches after
`replay_finish(true)`. **The flag deliberately survives a NEW-GAME
rotation's promotion until the next game over**: a best promoted while
discarding an abandoned run gets its offer at the next game over rather
than being lost (this is also what lets the e2e's game-over run use the
time-speed cheat — the offer and the upload belong to the earlier clean
best, not the flagged run that ended). The prompt rides the GAME OVER
card (`GLGame::board_*`, drawn by `Overlay::net_overlays`); `board_nav`
intercepts every game-over exit path (keyboard, controller dpad/A/B/
Start, right trigger, touch halves) behind the card's existing 3 s
grace, and the qualify has a 15 s deadline (with one silent reconnect on
a mid-qualify drop) so the card never waits on the network. The credential mint is warmed when the qualify fires (Steam's
ticket is async). The REPLAYS retry action moved to the LEADERBOARD
screen (see the revised decision above).
Headless e2e `test/e2e/leaderboard.sh` (a committed driver like
replay.sh — the wrangler-hosting cost keeps it out of linux.yml; the
worker suites gate deploy-board.yml instead): S1 clean personal best →
game over → prompt → YES → placed → the row reads back via `top` with
the header's exact score; S2 dead worker degrades silently (no prompt,
no error card, game alive); S3 cheat-only run produces no board traffic;
S4 `leaderboard_prompts=0` auto-uploads without a prompt (revised
2026-08-03 — the preference picks ASK vs AUTO, it never suppresses the
upload; a field tester read the old OFF as "no upload" after a missed
prompt).
**Exit**: e2e green headless; field pass on one desktop + one Android
device still owed (needs a deployed beta worker).

### L3 — leaderboard screen + watch ✅ (landed on this branch, 2026-07-31)
Implementation notes: the screen follows the replays list's grammar
(shared row geometry, TapBand exit, nav ladder — controller works via
the shared translator for free). Entries are [SOLO/CO-OP toggle row] +
[SEASON browser row] + top rows + [UPLOAD BEST RUN], so touch and
desktop confirm the same list; a/d flips the board from anywhere EXCEPT
the SEASON row, where a/d (and confirm) cycle the browsed season
instead. **Season browser** (added 2026-07-31 with the SEASON-file
scheme): the screen opens on this build's season
(`Replay::game_version_string()` — added because the Makefile scopes
the version stamp to replay.o) and fetches the worker's season list
(`{t:"seasons"}`, CANONICAL seasons only — the deliberate SEASON-file
stamps `s<N>`, decided 2026-08-01: dev/git-describe buckets are admitted
for upload but never listed, so they cannot clutter the browser every
player cycles through; newest submission first; the build's season is kept
at the front even before it has rows, tagged "- LIVE" when there is
somewhere else to cycle to). Old seasons' rows are viewable forever
(scores never expire; replays follow retention). **Replay downloads
are compat-gated BEFORE the fetch**: each row carries the replay's
`format`/`save_format` (stored at submit from the validated header),
and `net_board_replay_watchable` refuses rows outside this build's
Replay format range or savegame version range — the row's last column
shows OTHER VER (or NO REPLAY when retention demoted it), the footer
names the reason when such a row is selected, and confirm does
nothing; 0 (a row from before the columns existed) means unknown, so
the download proceeds and playback's own polite decline is the
fallback. The UPLOAD row appears on the screen of the season the
upload would LAND in (best.nrp's own) — an older build's best is
reached by cycling the SEASON row to it, replacing the old
"admitted but invisible" quirk. Own-row detection compares the rows'
`run_id` against best.nrp's ("- YOU" tag); the rank-of footer shows
only when the local best belongs to the browsed board (same season +
player count) and sits off the visible rows. Worker names are
re-sanitized through `net_sanitize_name` before touching the font —
the netplay identity path's security boundary, applied to worker data
too. Fetches land in `replays/download.nrp` (transient, never listed,
never best-checked) and hand off to the ordinary R2 playback. The
Options row shipped as "LEADERBOARD PROMPTS" (last, hidden when
`net_board_available()` is false — a toggle for a feature that cannot
appear would only confuse).
**Exit**: keyboard/controller/touch all drive list → watch → back; a
score-only or other-version row is visibly unselectable (tagged, no
confirm action, footer reason); menu-side `board:` logs are greppable.
The season browser + compat gate were verified by an xvfb/xdotool menu
smoke against a seeded two-season worker (one watchable, one save-18
row: cycle → tag shown → confirm inert → watchable season's fetch
fires); a committed headless menu-screen e2e is still owed — the L2
driver covers the transport and the worker end to end.

### L4 — deploy + season hygiene ✅ (landed on this branch, 2026-07-31)
`deploy-board.yml` mirrors deploy-signal.yml (tags → prod, master pushes
touching `board/**` or the shared `signal/src/*_verify.js` → beta,
manual dispatch either; gated on the L1 unit + protocol tests);
`board/README.md` carries the runbook. **Resource creation is automated**
(no Terraform — the footprint is four resources that never change shape):
`scripts/ensure-resources.sh`, invoked by the workflow before deploy,
creates the D1 database + R2 bucket for the target if absent and resolves
the real `database_id`, which the deploy step injects over the config's
placeholder (the placeholder stays in git; local `wrangler dev` ignores
it). The token needs D1:Edit + R2:Edit for that. Platform verify secrets
are still set per environment by hand (secret values — nothing can invent
them). The retention cron is unit-tested (`test/retention_test.mjs`
drives the real handler — live-season exemption, KEEP_N trim, dormant
strip) and locally triggerable (`/cdn-cgi/handler/scheduled`); a seeded
past-season verification against real D1 is owed at first deploy.
**Exit** (owed at first deploy): a `v*.*.*` tag deploys both workers,
auto-creating resources; beta isolation confirmed (beta rows never appear
in prod).

### Credential lifecycle ✅ (2026-07-31)
The upload's attestation credential is the SAME seam netplay uses
(`net_local_verify_credential`), and on the two single-use platforms
(Steam Web-API ticket, Play Games OAuth code) a credential validates once.
The backends already re-mint per read (each `credential()` returns the
last completed value and fires a fresh async request), so the client never
literally reuses the netplay credential — but because the re-mint is async,
a submit can still read an EMPTY (mint not landed), STALE (Game Center's
timestamp window) or already-CONSUMED value, all of which the worker
answers with `unverified`. So the board flow warms the credential when the
qualify fires and, on an `unverified` rejection, **polls for a genuinely
fresh credential and auto-retries the submit ONCE**: the submit's own
credential read already fired the next async mint, so the client peeks the
current value each tick *without re-minting*
(`net_board_verify_credential_peek` → the backends'
`local_verify_credential_peek`, which return the cached value without
firing a new request — critical on the single-use platforms, where a
per-poll re-mint would consume codes forever) until it sees a value
DIFFERENT from the one the worker rejected, then consumes and resubmits
exactly that once. A deadline (`BOARD_UPLOAD_RETRY_TIMEOUT_MS`, 6 s) gives
up as `unverified` if no fresh mint lands; one retry is also the
per-connection submit budget, so it can't loop. Both the game-over path
(`GLGame::board_tick`) and the menu UPLOAD path (`Menu::board_poll`) do
this. Game Center is not single-use (a signature verified within a
freshness window), so only the empty/stale cases apply there. Verified: a
worker `REJECT_FIRST_VERIFY` dev-var forces the first submit to fail
`unverified`; with `NEWTONIA_BOARD_TEST_CRED` the test-credential
simulator mints varying values (`<base>-<gen>`) on a delay
(`NEWTONIA_BOARD_TEST_CRED_DELAY` reads before the next value lands), so
the e2e proves both failure shapes end to end: S5 (consumed ticket — the
worker log shows the resubmit carried a genuinely DIFFERENT credential)
and S6 (mint not landed at submit — the empty credential is rejected,
the retry peek-polls until the mint lands, then places;
`test/e2e/leaderboard.sh`).

### Review pass ✅ (2026-07-31)
A six-lens multi-agent review (worker security/correctness, C++ seam
threading, game-over flow, menu screen, cross-layer protocol) with an
adversarial verify stage found 21 confirmed defects (2 critical, 6 major,
13 minor) — all fixed (commit on this branch). The two criticals were the
offline game-over prompt rendering nothing while still intercepting input
(a silent upload risk — `board_prompt` now owns the whole game-over card
in every mode) and the touch/click LEADERBOARD row launching a new game.
Majors: unmetered read/fetch paths (per-IP query/fetch limits added),
no verify-backend gate on the upload UI, concurrent submit+fetch wedging
the single-transfer seam, non-atomic dedup (UNIQUE index + atomic
ON CONFLICT upsert), and unsanitized worker strings reaching logs/font.
A bug beyond the review's scope was also fixed: the prompt navigated on
key-RELEASE, so a fire/thrust key held through death and released into the
prompt answered it — input now acts only on keys pressed while the prompt
is up. e2e `test/e2e/leaderboard.sh` remains green after all fixes.

### Security review (2026-08-03) — attestation / upload / download / worker
A focused second pass over the four surfaces the feature actually exposes:
the platform-attestation path (`signal/src/*_verify.js` + the board's
`verify_identity`), the submit path, the download/playback path, and the
worker's own budgets. Findings below, most severe first, with a status
marker each. Nothing here contradicts the L5/L6 posture — the deliberate
"header is the truth, watchability is the deterrent" tier is called out
where it bounds a finding rather than being one.

**S1 — TLS verification is disabled on a socket that carries credentials
(`net_board_rtc.cpp` `connect`).** ✅ FIXED (2026-08-03, below) The flag was
inherited verbatim from
`net_signal_rtc.cpp`, where the rationale holds: the p2p channel is
authenticated by DTLS/SDP fingerprints, so the signalling socket carries
nothing security-bearing. `/board` is different — it carries the platform
verification credential (Steam Web-API ticket, Play Games single-use
server auth code, Game Center signature bundle), the replay published
under the player's account, and the blob playback then parses. An on-path
attacker can present any certificate, capture the credential and DROP the
victim's submit, then spend that credential against the real worker to
place a forged score under the victim's attested account and name — the
single-use property protects nothing when the MITM controls whether the
legitimate use ever happens, and a Game Center bundle needs no race at all
(no single-use binding, replayable for the full 10-minute freshness
window). Two aggravating details: the flag is unconditional, so it also
disables verification on the builds that link OpenSSL and could verify
today (`build_netplay_deps.sh`'s non-`--universal` path — only the
`--universal` macOS branch and Windows use MbedTLS); and the "the C API has
no CA-file field" comment predates the pinned libdatachannel (v0.24.5),
whose `rtcWsConfiguration` exposes `caCertificatePemFile` — to be confirmed
against the pinned header. Note the same credential already rides the
signalling socket under the same flag, so the exposure predates the board;
what the board adds is the durable, public, account-attributed artifact at
the end of it.

*Fix (both sockets — one policy, `net_tls.h`).* Verification is now ON, with
the roots CARRIED rather than borrowed, because half our platforms have no
trust store to borrow from. Four parts:

1. **`patches/libdatachannel-ws-ca-cert.patch`** (9 lines). Upstream exposes
   `caCertificatePemFile` only on the C++ `WebSocketConfiguration`; the game
   speaks the C API, whose `rtcWsConfiguration` has no CA field on v0.24.5 or
   master (`capi.cpp` maps six fields and skips this one). The patch adds the
   field, maps it, and relaxes the `#ifdef _WIN32` block to fall back to
   unverified only when NO CA was supplied — Windows lacks a *trust store*
   (OpenSSL does not read the CryptoAPI ROOT store and its default verify
   paths are a Unix path baked in at build time), not the ability to verify,
   so a supplied bundle IS the missing store. Applied by every path that
   builds the library: `build_netplay_deps.sh`, `build_netplay_deps_ios.sh`,
   both FetchContent `CMakeLists` (via `cmake/apply_patch.cmake`, which
   reverse-checks so a re-populate is idempotent), and the four workflows
   that clone it themselves (windows, deploy-steam, ios, deploy-ios). A build
   that misses the patch fails to COMPILE on the unknown field — the loud
   failure, not a silent unverified socket. Upstreamable as-is; the fork
   retires when it lands.
2. **`net_ca_bundle.cpp`** — the Mozilla root program (curl's build, 119
   certificates), embedded as chunked string literals and regenerated by
   `generate_ca_bundle.py`. Deliberately the FULL bundle, not the four CAs
   Cloudflare currently issues from: they pick a CA per certificate and
   rotate (Let's Encrypt / Google Trust Services / SSL.com, plus Sectigo for
   backups), so a narrow set is a silent tripwire years out — and the anchor
   pinning it would buy is worth little when the pin has to include four of
   the largest public CAs anyway. The win is VERIFICATION, not pinning.
3. **`net_tls.cpp`** — materializes the bundle into the SDL pref path on
   first connect (rewritten when the size differs, i.e. after an update) and
   hands libdatachannel the path. OpenSSL takes a path only; MbedTLS would
   accept inline PEM, but one code path beats two. Where a real system store
   exists (Linux, non-universal macOS) OpenSSL has already loaded it and ours
   simply adds to it.
4. **Failure is terminal, with an escape hatch.** A refused handshake is just
   a failed connection (LEADERBOARD UNAVAILABLE / the usual netplay connect
   error); there is deliberately no retry-unverified, which any attacker
   could force by breaking the first attempt. `NEWTONIA_NET_TLS_INSECURE=1`
   restores the old behaviour for field debugging behind a loud log line, and
   is set by no shipped build.

Verified: the `test/tls/` gate (now run by `linux.yml`) stands up a local TLS
WS server and proves the three outcomes — correct CA connects, **unrelated CA
refused** (`certificate verify failed`), insecure flag still connects; the
bundle materializes to the pref path and OpenSSL parses all 119 certificates
back out; and the full Linux game builds and passes `NEWTONIA_NET_SELFTEST`
against the patched library. **Field-verified on every platform this repo
ships** (TESTING.md's per-platform matrix): Linux/OpenSSL via the CI gate,
then real handshakes against the production worker from macOS universal,
Android, iOS (all MbedTLS, where our roots are the ONLY trust source) and
Windows (2026-08-03/04). The Windows run doubles as the field proof of the
patch's second hunk, since upstream refuses to verify there at all. Residual:
the patch is a fork until upstream takes it, so a libdatachannel bump must
re-check the three hunks still apply — and Windows is the row to re-run
first, being the only one that degrades to UNVERIFIED silently instead of
failing closed.

**S2 — a failed INSERT orphans the R2 blob forever (`board/src/worker.js`
`finish_submit`).** ✅ FIXED (2026-08-03, below) `REPLAYS.put` ran BEFORE the
upsert, and retention
walks rows only — so an object whose row never lands is unreachable and
never reaped, while the generic `catch` reports a bare `internal`. Not
merely a D1-hiccup path: the upsert targets `(season, players,
platform_key)` but the table also has `PRIMARY KEY (season, run_id)`, and
SQLite aborts when the violated constraint is not the conflict target.
Confirmed both ways against SQLite:
`same account / same run_id / same players` upserts cleanly, while
`same account / same run_id / players 1 then 2` throws
`UNIQUE constraint failed: scores.season, scores.run_id`. That second case
clears every prior guard — the `run_row` check matches on `(season,
run_id)` and finds the submitter's OWN key, and the `mine` query filters
`run_id != ?4` — and both fields are attacker-chosen header bytes, so it
repeats at the submit limit with a ≤32 MB orphan each time. Fixes: store
the blob only once the row has won (or delete it on any failure), refuse
the cross-`players` `run_id` collision explicitly like the cross-account
one, and add a reaper for keys with no row.

*Fix.* All three, in that order of importance:

1. **The collision is refused where every other `run_id` collision is** —
   `run_row` now selects `players` too, and a mismatch answers
   `already-submitted` before anything is stored. With the score check and
   the `platform_key` check already there, no surviving path can reach the
   INSERT with a row that collides on the PK but not on the upsert's target,
   so the abort is unreachable rather than merely handled.
2. **Every exit past the `put` accounts for the blob.** The row work moved
   into `place_row()`; the caller wraps it so ANY throw (a D1 outage, a
   constraint nobody predicted) deletes the blob before unwinding. The
   not-best race already deleted its own and still does.
3. **A daily orphan sweep backstops both** (`sweep_orphans`, called from
   `scheduled()`): one query for the `blob_key`s D1 knows, one paginated R2
   walk, delete anything unreferenced and **older than a 24 h grace**. The
   grace is correctness, not politeness — a submission in flight has stored
   its blob and not yet inserted its row, and sweeping that would break a
   live upload. It is also the only thing that can clean up whatever leaked
   before this fix, since an orphan is invisible to a row-walking cron.

Verified: `board_test.mjs` gained the cross-board `run_id` case (refused
cleanly, and the co-op board stays empty — no half-written row), which was
confirmed to FAIL against the pre-fix worker with exactly the reported
`{"t":"err","reason":"internal"}`; `retention_test.mjs` gained the sweep
cases (stale orphan deleted, in-flight upload spared, undated object spared,
referenced blob untouched). Full board suite green: units,
`board_test.mjs`, and `whitelist_test.mjs` against `wrangler dev --local`.

**S3 — the header's score is never cross-checked against the recording
(`board/src/validate.js` `validate_submission`).** ✅ CLOSED (2026-08-03,
below — as an observation, deliberately not a gate) (bounded by L5/L6 —
this is the accepted tier, not a defect.) Editing one `u32` at header
offset 52 charts any score. Worth noting anyway because a cheap partial
sits unused INSIDE the current design: `walk_records` already computes
`last_slot`, `records` and `keyframes`, and `validate_submission` discards
all of it except `deltas === 0`. Requiring `duration_ms` to agree with
`last_slot * 100` (and `generation` to be plausible against the record
count) makes the trivial hex-edit inconsistent with its own file — still
forgeable, but a real bar where there is currently none, and it sharpens
the social deterrent rather than replacing it.

*Outcome (Glenn, 2026-08-03): logged, never enforced — and no score cap.*
Investigating the invariant properly changed the answer, so the reasoning is
worth keeping:

- **The invariant is exact.** `duration_ms` is not measured, it is DERIVED:
  `header_.duration_ms = (last_slot_ + 1) * 100` (`replay.cpp`). So the
  worker can compare it against the walk it already performs.
- **An online rejoin does NOT break it** (the first thing Glenn asked, and
  the right question). Every drop path — the pre-keyframe hold that
  `await_keyframe()` reopens on a client rejoin, the size cap, a failed
  write — returns BEFORE the `last_slot_++`, so records held out never
  advance the timeline. The slot line stays dense and a P2 reconnect simply
  yields a replay shorter than wall-clock. Only `REC_EVENTS` wobbles it, by
  stamping the UPCOMING slot, so a file ending on an event carries a max
  slot one above the header's count.
- **But a legitimate file CAN fall outside it.** The header is patched at
  each clean stop and recording continues afterwards, so a process that dies
  before the next patch leaves a stale header over newer records — and that
  file still carries FLAG_CLEAN, so `maybe_promote_best` promotes it
  happily. Corners like that cannot be enumerated with confidence, and a
  player silently losing a real run is a worse outcome than the fabricated
  file the gate would have caught.
- **The check is no defence against the actual threat regardless.**
  `final_score` is copied from live game state at patch time and has no
  structural correlate: a file with one hex-edited `u32` at offset 52
  satisfies the duration band, the record walk and the keyframe cadence
  alike. Fixing THAT is L5 or L6, not this.
- **No score cap either.** A ceiling would only stop absurd vandalism (a
  `u32`-max row parked at #1), not a forger picking a plausible number — and
  vandalism is one D1 delete away from fixed.
- **Parsing the keyframe's savegame body in JS was rejected outright.** It
  is the only real server-side answer available, but it duplicates the
  savegame format in a second language and would start refusing every new
  submission the day `GameState::VERSION` bumps and the worker lags. That
  failure mode is worse than the threat.

So `shape_note()` computes the band and the worker LOGS `submit shape: ...`
while admitting the submission regardless. That turns L5's trigger ("nothing
built until forged submissions actually appear") from a guess into
something observable, at zero risk to any player; tightening later is a
one-line change with evidence behind it. Verified: `validate_test.mjs`
covers the consistent file, the trailing-event wobble, a fabricated duration
and a stale header (both NOTED and ADMITTED), and a part-slot duration. The
shared `nrp_fixture` default was made self-consistent so a note appearing in
a protocol-test log means something — the full `board_test.mjs` run now logs
zero.

**S4 — iOS rows publish a fully self-chosen display name** (`worker.js`
`verify_identity` platform 4 → `{name: claimed, verified: false}`, rendered
as-is by `menu.cpp`). ✅ ACCEPTED as-is (Glenn, 2026-08-03): iOS names stay
unattested and stay rendered, without a tick — no code change. The
2026-08-01 gamertag-rule decision stands; this pass only re-examined it.
Two facts make that comfortable. The tick is **unforgeable by
construction**, not by convention: `Typer::VERIFIED_TICK` is `'\x01'`, a
control byte, so `net_sanitize_name` strips it from every wire name AND
`net_name_char_drawable` excludes it from the glyph whitelist — two
independent lines, both commented as security rather than cosmetic. And
admission still required a cryptographically proven Apple account, so an
impersonating row is attached to a real, bannable identity and one D1
delete away from gone. Reversing costs little if impersonation ever shows
up in the field: it is the display rule in `menu.cpp` plus what
`verify_identity` attests, not a protocol change. Original finding, for
that day: correct that Apple exposes no alias lookup; the
question is the DISPLAY decision (2026-08-01, deliberately the looser
"gamertag rule"). A player can put another player's Steam persona on the
board, distinguished only by the ABSENCE of the verified tick — a signal
read by nobody who does not already know to look for it, and the IOS badge
does not say "this name is unverified". Options: badge + role label for
iOS (matching the online-peer identity rule), or a positive unverified
marker instead of a missing one.

**S5 — per-connection budgets are in-memory on a hibernation-enabled DO,
and the per-IP limiter fails open (`worker.js` `Session`,
`within_limit`).** ✅ FIXED (2026-08-03, below) `state.acceptWebSocket` opts into hibernation while
`queries`/`submits`/`fetches` and the in-flight `upload` live in
constructor-initialised fields, so an eviction resets them; the 10-minute
idle `setTimeout` probably keeps the DO resident, but nothing enforces
that. The per-IP `Limiter` is therefore the only durable bound — and it
returns `true` on any limiter error, by design ("a limiter hiccup must not
lock everyone out"), so pressure on the Limiter DO relaxes it. Reasonable
for reads; `submit` should fail CLOSED.

*Fix.* Both halves, and the second one is the more interesting.

**`submit` now fails closed.** `FAIL_CLOSED` lists it; every other action
keeps the old fail-open behaviour, and either way the outcome is logged
rather than silent. The asymmetry is the point: the per-IP window is the
only DURABLE bound on submissions (the per-connection cap resets on
reconnect by design), so failing open there lifts the submit ceiling
entirely for the duration of an outage, and a submit is the expensive path
— an R2 write plus D1 rows. A refused submit also costs the player
nothing: the upload row offers TRY LATER and `best.nrp` holds the run until
it lands. Reads stay open because locking the whole board out over a
limiter hiccup is the worse failure.

**Budgets now ride the socket, not the instance.** `Session.hydrate()` /
`persist()` keep `{ip, dev, queries, submits, fetches, forced}` in the
WebSocket attachment — the thing that survives hibernation by design —
restoring once per instance and persisting whenever a budget moves (so
chunk frames, which touch no counter, cost nothing). The in-memory object
stays authoritative after that first hydrate, which is what keeps
interleaved handlers race-free: a message delivered while another awaits a
verify shares one set of counters rather than racing a read-modify-write.
Worth recording WHY this needed doing at all, since the old code was not
actually broken: a pending `setTimeout` keeps a DO resident, so the idle
timer means hibernation never happens today. The budgets were correct **by
accident of an unrelated timer**, and would have gone quietly wrong the day
someone converted that timer to the storage alarm the hibernation docs
recommend. That conversion is now a safe change to make on its own merits,
and the `CONN_IDLE_MS` comment says so instead of asserting the old
"nothing to hibernate for".

The in-flight upload deliberately does not ride along — 32 MB of chunks is
far past what an attachment may hold — and does not need to: a chunk
arriving with no `upload` already answers `bad-frame` and closes, a clean
refusal rather than a silently truncated replay.

Verified: new `budget_test.mjs` (wired into `deploy-board.yml`) pins the
fail-closed/fail-open split across both breakage shapes (a throwing limiter
and one returning garbage), and pins the budgets surviving a
hibernation-style reconstruction — a second `Session` built from the same
socket restores all six fields and still refuses past the cap — plus
hydrate-twice being a no-op and a missing/throwing attachment leaving
defaults instead of crashing the handler. The protocol test's existing
`per-conn submit budget` case confirms the caps still bite end to end.

**S6 — minor, worker.** ✅ FIXED (2026-08-03, below) (a) `submit refused: unverified
platform=${msg.platform}` logs the RAW client field (any bytes, up to the
32 KB frame cap) instead of the coerced number computed one line above —
the one unsanitised log token left. (b) The dev switches `FAKE_VERIFY`,
`REJECT_FIRST_VERIFY`, `SUBMIT_LIMIT`/`CONN_LIMIT` are gated on env vars
alone: correctly absent from `wrangler.toml` and passed only by
`wrangler dev`, but one stray dashboard variable turns production into an
open board, and the fake path logs the credential prefix. A second
condition (refuse the fake path on the production script name) makes that
unreachable by misconfiguration.

*Fix.* (a) The log line interpolates the COERCED number, and `log_str()`
now names the boundary for any client text that reaches a log — printable
ASCII only, length-capped, the mirror of `net_board_sanitize` on the game
side. It is used for the dev credential log too. (b) Every dev switch
gained a second condition, `is_dev_host()`: the request must have arrived
on a loopback hostname, or `FAKE_VERIFY`, `REJECT_FIRST_VERIFY` and the
widened `SUBMIT_LIMIT`/`CONN_LIMIT` windows all do nothing. Configuration
alone can no longer disable attestation, because the deciding input is
where the request came from, not what the environment says. Cloudflare
routes by hostname, so a forged `Host` cannot reach a deployed worker
wearing a loopback name; a remote `wrangler dev` preview is deliberately
not "dev" either. The flag is decided once in `fetch()` and passed to the
Session DO and the Limiter, so all three switches read the same answer.

Verified end to end against a real `wrangler dev --local` running WITH
`FAKE_VERIFY:1`: reached over `127.0.0.1` a submit answers `submit-ok`
(dev path intact), and the SAME worker reached over a non-loopback
hostname pointed at the same port answers `unverified` — the fake
attestation is inert. `identity_gate_test.mjs` pins both directions plus
the lookalike hostnames (`localhost.evil.com`, `notlocalhost`,
`127.0.0.1.evil.com` are all non-dev) and the `log_str` boundary
(newline, control bytes, non-ASCII, length, non-string input).

**S7 — anyone can pre-claim a co-op client's derived run_id.** ✅ ACCEPTED,
not fixed (Glenn, 2026-08-03 — a salted derivation was built, reviewed and
REVERTED; reasoning below). `finish_submit` refuses any run_id already held
by a different `platform_key`, and the client records under `~run_id_`, so
a crafted blob carrying that id blocks the run permanently.

*Reach is wider than "a malicious partner".* The board PUBLISHES run_ids —
they are the fetch key, on every `top` row, used by `menu.cpp` for downloads
and the `- YOU` tag. A charted HOST row therefore hands any stranger the
partner's id by inspection; submit a crafted blob carrying it from any
attested account and that partner can never upload that run. Symmetric (a
charted client row exposes the host's id), and the window stays open against
anyone who has not uploaded yet.

*No cheap worker-side fix exists.* Any rule that would admit both rows still
meets `PRIMARY KEY (season, run_id)`, so a cross-account collision MUST be
refused whatever logic sits above it — relaxing that means changing the
primary key, which SQLite can only do by rebuilding the table. Content-hash
dedupe (equally strong against verbatim theft, not squattable) hits the same
wall. Worth knowing before anyone reopens the dedupe design: it costs a
live-table migration.

*Why the client-side fix was reverted.* Salting the derivation
(`run_id_ ^ per-install secret`) was implemented and worked, but review
found the secret is RECOVERABLE from public data: both peers of a session
chart two rows whose run_ids are `R` and `R ^ salt`, so `salt = R ^ rec_id`
falls out of any charted pair — and the pair is easy to spot (same season,
same generation, adjacent submit dates). After that the victim's id is
predictable again in every later session where they are the client. The
constructions that resist this need the key inside a one-way function
(SipHash or similar, ~40 lines of crypto in every build) or a persisted
`{host_run_id → random rec_id}` mapping, which brings its own degradation —
one mapping slot means a client alternating between two hosts stops
resuming its recording on rejoin.

Against that: the impact is one run that cannot be uploaded. Nothing is
stolen, no score forged, nothing lost — the victim plays another run. The
attacker spends an attested account's row slot for the season to do it, and
one D1 delete undoes it. Chasing the last of it was judged not worth the
complexity, so the salt was reverted and the behaviour stands as described.
The deterrent stack that remains is the one L6 lists: run_id dedupe, one row
per account, attestation, submit rate limits, and watchability.

**Held up well:** attestation is checked BEFORE any bytes are accepted, so
a spoofed submit costs the sender a round-trip and us nothing; the header
is a single source of truth with no claimed score to reconcile; run_id
dedupe, the UNIQUE index + atomic conditional upsert, the chunk-COUNT cap
beside the byte cap, and the verify deadline are all the right shape;
Apple's cert fetch is host-pinned with a tight freshness window; Steam and
Play Games names come from the platform API, never the wire; no secrets in
git. Client side: every worker-controlled string is sanitised before it
reaches `SDL_Log` or the font (`net_board_sanitize`, `net_sanitize_name`,
and the season list), the download size is bounded before buffering, and a
downloaded replay lands in the hardened reader — `Reader` caps file size
and record bounds and `deserialize_game` bounds every container count
before resizing, which is what makes "the worker does not validate
payloads" a defensible split rather than a hole.

### L5 — deferred: verification
R5's input-log re-simulation, arriving through the reserved `verify`
envelope field. Nothing built until forged submissions actually appear;
the social deterrent (every row is watchable) is the v1 defense.

### L6 — potential: run witnessing (replay-theft binding) — NOT locked in
Sketched with Glenn 2026-08-02, deliberately unbuilt. The threat: replays
are public and nothing in a file proves WHO played it, so an attacker
with a real attested account can hex-edit a downloaded (or copied)
replay's run_id and submit a score they didn't earn.

Pure cryptographic binding is impossible from this trust model: the
client is untrusted (anything it signs over its own file it can sign
over a stolen one), and no platform attestation primitive (Steam ticket,
Play Games code, Game Center signature) will sign arbitrary application
data — they prove account PRESENCE, never authorship of bytes.

The practical near-equivalent is TEMPORAL WITNESSING — shift the proof
from "who signed the file" to "whose account the worker watched this run
being created by":
- at game start (or first checkpoint) the client REGISTERS the run_id
  with the worker under its attested account;
- during play it sends a rolling hash of the recording-so-far (one tiny
  frame every few minutes);
- at submission the worker requires the file to match the witness chain:
  registered by the submitting account, checkpoint hashes consistent
  with prefixes of the submitted bytes, registration older than the
  run's duration_ms.
A thief cannot retro-register (timestamps predate them), cannot have
produced the incremental hashes during the original play, and editing
run_id breaks the chain — defeating both download-resubmission and
copied-save-dir theft with no client-side crypto to forge.

Costs that keep this OUT of scope until score theft is a live problem:
runs played offline can never be witnessed (either they cannot chart or
a lower-trust tier appears), every run adds worker writes (free-tier
budget + rate-limit design), and it is a real protocol addition on both
sides. The current deterrent stack is proportionate for now: run_id
dedupe (cross-account resubmission cleanly refused), one row per
account, attestation (a real, bannable platform account per row),
submit rate limits, and watchability — a stolen top run is literally
the original video under a different name, and one D1 delete fixes it.

### L7 — potential: the RECORD REPLAYS trap — NOT locked in
Root-caused in the field 2026-08-03: the leaderboard can only offer what
was recorded, and recording is opt-in (OFF by default — a REPLAY.md ship
posture driven by web's IndexedDB quota). A player with the option off
gets NO game-over prompt and NO upload row, silently — the host of a
co-op session sat mystified while the (recording-on) phone charted.
Diagnosis checklist that found it: empty REPLAYS menu after a game =
nothing recorded. Possible remedies, deliberately deferred (Glenn may
deal with it in the future): default RECORD REPLAYS to ON for native
builds (disk is cheap, files rotate; web keeps its quota-driven opt-in),
and/or surface the dependency — "TURN ON RECORD REPLAYS TO ENTER
SCORES" on the LEADERBOARD screen / game-over card when recording is
off. Until then this is a known support question.

## Open questions

None — every question raised during planning has been resolved into the
decisions above (prompt default YES, attestation required, co-op credit
to the submitter, no web in v1, rank-of footer included).
