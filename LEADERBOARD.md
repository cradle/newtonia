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
  itch `newtonia-online` alike. `NetBoard::create()` returns null under
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
  header row. Selecting a `has_replay` row downloads with progress and
  hands `download.nrp` to `start_replay_playback`; rows without a blob are
  unselectable (score-only). Shared nav ladder + TapBand geometry
  throughout — no hand-rolled input.
- **Prompt fatigue valve**: an Options row "LEADERBOARD PROMPTS ON/OFF"
  (default ON). OFF suppresses the game-over prompt only; the REPLAYS
  UPLOAD action always works.

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
grace, and the qualify has a 4 s deadline so the card never waits on the
network. The credential mint is warmed when the qualify fires (Steam's
ticket is async). The REPLAYS retry action moved to the LEADERBOARD
screen (see the revised decision above).
Headless e2e `test/e2e/leaderboard.sh` (a committed driver like
replay.sh — the wrangler-hosting cost keeps it out of linux.yml; the
worker suites gate deploy-board.yml instead): S1 clean personal best →
game over → prompt → YES → placed → the row reads back via `top` with
the header's exact score; S2 dead worker degrades silently (no prompt,
no error card, game alive); S3 cheat-only run produces no board traffic;
S4 `leaderboard_prompts=0` produces none either.
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
