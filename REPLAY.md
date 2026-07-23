# Replays — Design & Implementation Plan

Planning doc for the replay system (leads into the leaderboard work). Style
mirrors NETPLAY.md: decisions first, then phased milestones with exit
criteria. Nothing here is built yet.

## Decisions (locked with Glenn 2026-07-17)

- **Format: snapshot stream, not input log.** A replay file is the netplay
  host's snapshot feed written to disk — keyframes + deltas + EV_* events in
  the same encodings the wire uses. Playback is the existing net client
  reading from a file instead of a transport. Rationale: zero determinism
  requirements (an input log is invalidated by ANY sim/balance change and
  silently diverges; a snapshot stream is recorded outcomes — an old replay
  shows the old balance, which is correct for a historical run), perfect
  cross-platform playback, instant seek at keyframes, and it reuses the
  net-client machinery that months of "invisible on the client" fixes have
  already hardened. Cost: a few MB per long run vs tens of KB — fine.
- **Always record, never ask.** Recording starts silently at every new solo
  game (kilobytes-per-second budget). No "save replay?" prompt anywhere.
- **Auto-keep three local replay files — all three watchable**:
  `replays/current.nrp` — the active (live or resumable) run; watching it
  from the menu is allowed and useful — review what just happened before
  diving back in. Safe because playback only ever starts from the menu, and
  by then `current.nrp` is closed and its header was patched at the clean
  abandon; the only stale-header file a viewer can meet is a crash artifact,
  which the reader tolerates anyway (see the storage model).
  `replays/recent.nrp` — the most recently COMPLETED run;
  every run lands here when it ends, cheat-flagged or not (useful for
  debugging). `replays/best.nrp` — a copy promoted when a completed,
  non-cheat-flagged run's final score beats best's header score.
  **Rotation**: game over patches `current`'s header, runs the best check,
  and renames it to `recent`. Abandon-to-menu patches the header but keeps
  `current` in place — the run is still live and resumable. Confirming NEW
  GAME over an existing run first rotates the old `current` into `recent`
  as-is (an abandoned-forever run is never silently lost), then starts a
  fresh `current`. The best check runs only where the header is known
  accurate: at game over, and at NEW-GAME rotation of a cleanly abandoned
  run (its header was patched at abandon). A run that CRASHED and is then
  discarded via NEW GAME rotates into `recent` with its stale header and
  gets NO best check — accepted limitation: promotion can't be evaluated
  inside a de-focus window (no time budget there), and if a best-worthy run
  crashes and the player picks NEW GAME, it's just too bad — the run itself
  still survives in `recent`, it can only never become `best`.
- **Replays are run-scoped, not session-scoped — resume continues the same
  replay.** A new solo game stamps a random `run_id` into both the savegame
  (`GameState`) and the replay header. Exiting to the menu patches
  `current.nrp`'s header but does NOT close the run. On CONTINUE, if the
  loaded save's `run_id` matches `current.nrp`'s, the recorder appends to
  that file — starting with a fresh keyframe as a resume seam (sim time
  continues) — so an exit→continue cycle produces ONE continuous replay, not
  a fragment. A mismatched or absent `run_id` starts a fresh recording from
  that keyframe. This is what keeps `best.nrp` a *whole* run: without it,
  resuming and then beating the old score would promote a `best` that opens
  mid-run on the resume keyframe with the score already high, and the
  earlier gens would be lost.
- **REPLAYS menu row** on the main menu below OPTIONS → a list screen (LAST
  RUN / BEST RUN with score, level, date) → playback. Same nav-ladder +
  TapBand patterns as every other screen.
- **Version discipline**: replay files are stamped (magic `NWRP`, format
  version, game version string) and the recorded encodings adopt the
  savegame's append-only + version-gate convention the moment R1 lands.
  Live netplay keeps its strict PROTO match; only the replay reader needs
  tolerance. Playback of a file whose format version is out of range
  declines politely ("REPLAY FROM AN OLDER VERSION") — last/best regenerate
  within a session, so this only matters across breaking format changes.
- **Leaderboards are seasonal anyway** (scores across balance changes aren't
  comparable), so replay compatibility only ever needs to hold within a
  season = a version window with no format break. Old seasons keep replays
  best-effort, eventually score-only.
- ~~**Solo games only in v1.**~~ **Online games record too (added
  2026-07-20, Glenn: host AND client, "even if it might contain gaps from
  disconnects").** Both roles record, each machine writing what its own
  screen was fed: the host tees the exact keyframe/delta payloads it builds
  and sends (no second build — the builders' shared delta baseline forbids
  two callers), the client tees the reassembled stream it receives plus a
  self-built bootstrap keyframe (the lobby consumed the wire one before the
  game existed). Consequences of "what this machine saw": the client's own
  ship appears as the host's reconciled view of it (not the buttery local
  prediction), and an effect that never replicated in live play (e.g. the
  peer's nova ring, which is host-local) is absent from the other side's
  recording too — the replay is honest to that machine's session.
  Remote-player effects arrive as MSG_LANCE/MSG_SHOCK/MSG_SHOT and are teed
  at their receive sites; received EV_* events tee with the same skip list
  as the send tee. **File: `replays/online.nrp` — the REPLAYS menu's own
  ONLINE RUN row (a 4th slot, Glenn 2026-07-20)** — deliberately separate
  from `current.nrp` so hosting/joining mid-way through an offline run
  never rotates that run's resumable recording away. It never rotates
  anywhere: ended or abandoned, the session stays listed as ONLINE RUN
  until the next online session overwrites it (exactly like `recent` is
  overwritten by the next completed offline run — and unlike the earlier
  design, an online game can no longer clobber the offline LAST RUN).
  The best check runs at ended-finalize and again on a cleanly-closed
  leftover about to be overwritten (the twin of the NEW-GAME rotation
  check); a crashed session's stale header gets none — same accepted
  limitation as offline.
  **Disconnect gaps**: slots are emission counts, not wall clock, so a
  disconnect doesn't freeze the timeline — it compresses out, and the world
  jumps at the seam keyframe. The host records straight through a
  rejoinable loss (the cadence and builders keep running; only sends are
  skipped). A client whose auto-rejoin rebuilt the GLGame APPENDS to the
  same file: `run_id` rides every snapshot (GameState v17), so the new
  game's `replay_start` finds the matching id and continues the slot
  numbering — the same resume seam as offline exit→continue. Dev quirk:
  two instances sharing one machine/pref-dir (loopback testing) both write
  the same `online.nrp` and interleave; e2e drivers isolate
  `XDG_DATA_HOME` per instance. A 2-player replay
  records both ships and plays back in split-screen (R2); the header's player
  count drives which.
- **Input-log verification is deferred** until leaderboard cheating actually
  materialises. The submission format reserves room for it (checksum field)
  but no determinism work (sim-RNG split, libm hardening) happens now.

## File format — `replays/*.nrp` (pref path, IDBFS on web)

```
header:  magic "NWRP" | format version | game version string | run id |
         flags (cheated, aborted) | final score | generation reached |
         duration ms | player count | date
records: [slot index | kind | payload] ...
         kind = KEYFRAME (full state, every 10th slot, same builder as
                net_send_snapshot's keyframe path)
              | DELTA    (net_send_delta payload)
              | EVENTS   (the EV_* outbox for that slot — sounds, banners,
                          booms, pickups; what makes playback *feel* right)
```

- Records are slot-indexed (the 10 Hz snapshot cadence), not wall-clock:
  pauses simply emit no records, so the playback timeline is pure sim time.
- **Storage model: append at checkpoints; header patched in place, never a
  whole-file rewrite** (all platforms). Records since the last flush
  accumulate in an in-RAM `Save::MemStream` chunk (the type netplay already
  builds snapshots into); each checkpoint APPENDS that chunk to
  `replays/current.nrp` and drops it from RAM. Appending bounds BOTH the
  per-flush write and the RAM footprint to roughly one level's worth of
  records regardless of run length — the bound that keeps the Android
  `onPause` budget and the ~1 s Xbox suspend TCR safe (see Open questions),
  and eliminates any 10 Hz disk I/O on the game thread. Checkpoints:
  **level clear**, the same pause / background / focus-loss points the
  savegame already auto-saves at (on iOS this rides the
  `SDL_APP_WILLENTERBACKGROUND` / resign-active save hook), and run end.
  Level clear is the load-bearing proactive one: it's already a savegame
  auto-save point, it lands inside the frozen 5 s clear countdown /
  generation rebuild (no combat to hitch), and it drains the buffer at every
  level boundary so the reactive lifecycle appends stay tiny. Levels 1–15
  all have an intro screen — the world frozen and idle for up to 5 s, the
  slackest window of all — so through the early game the flush should land
  on the intro in preference to the bare level-clear instant (the recorder
  survives the ownership transfer into the `Intro` state — it's owned by
  `GLGame`, which the `Intro` friend holds — so it's available to flush
  there). From level 16 on there's no new object type and hence no intro, so
  those levels flush at the level boundary itself. Web appends to MEMFS and
  fires `FS.syncfs`→IndexedDB at the same checkpoints.
  **The header goes stale between patches — by design.** It is written once
  at file creation (`run_id` and date are immutable from then on); its
  fixed-size tail fields (score, duration, flags, generation) are patched in
  place only at game over or a clean abandon-to-menu — both outside any
  lifecycle time budget. Between patches the header understates the records
  behind it. That is acceptable because playback only starts from the menu,
  where `current.nrp` has just been patched by the clean abandon — the only
  stale-header file a reader can meet is a crash artifact (plus the resume
  path, which needs only the immutable `run_id`). Playback therefore derives
  total time from the final record's slot index, not the header's duration
  field; a stale header at worst understates the score shown on the replay
  list row. File validity comes from self-delimiting records, not
  rewrite atomicity — a flush cut mid-append leaves a truncated final record
  the reader detects and drops (R2's reader needs truncation tolerance for
  crash artifacts regardless). Durability equals the savegame's: a hard
  crash loses only records since the last checkpoint (the background append
  covers Android's silent kill of a suspended process), and the surviving
  file is a free crash-repro artifact up to that point.
- **Resumed games work naturally**: the first record is always a keyframe
  (full world state), exactly like a rejoining net client's bootstrap. A
  resume *within the same run* (the save's `run_id` still matches
  `current.nrp`'s) appends that keyframe as a seam to the existing file so
  the replay stays continuous across exit→continue; a resume whose `run_id`
  no longer matches (e.g. `current.nrp` was already rotated or belongs to a
  different run) starts a fresh recording from that keyframe.

## Milestones

### R1 — recorder ✅ (landed on this branch)
Implementation notes, where reality refined the sketch: slots are the
recorder's own 10 Hz emission count (continued across resume by scanning the
file's last slot) rather than a sim-clock derivation — `current_time`
advances during pauses, so deriving slots from it would have put pause-length
gaps in the timeline; header `duration_ms` = slot count × 100 (pure play
time). The EV tee lives at the top of `net_send_event` and skips
EV_PAUSE/RESUME (pauses emit nothing), EV_BYE (transport lifecycle), and
EV_ACHIEVEMENT (playback must never poke a platform SDK);
EV_GENERATION_START's call site moved out of its NetHost gate so solo runs
record the marker. `run_id` is savegame v17 and bumped netplay PROTO to 23
(snapshots serialize through the same structs). The recorder starts lazily
on the first tick — the net ctors delegate to the offline ctors, so
construction can't know the game is offline. Exit criteria are enforced by
`test/e2e/replay.sh` (+ `replay_check.py`), all green headless.
`replay.h/cpp`: a `ReplayRecorder` owned by GLGame (records every solo game,
cheat-flagged or not). Hooks: the 10 Hz slot cadence calls the same
keyframe/delta builders `net_host_send_snapshot` uses, but appends the record
to the recorder's in-RAM `Save::MemStream` chunk (see the storage model
under File format) instead of sending it — the chunk is appended to
`current.nrp` at the checkpoints listed there, not written per slot. The
snapshot builders move behind a small seam so host-send and recorder share
them (they must never fork).
- **EV capture needs its own tee — the net outbox does not exist offline.**
  `net_send_event` early-returns `if (!net_session_) return;` (glgame.cpp),
  so in a solo game the EV_* events (EV_PICKUP, EV_WORLD_BOOM, banners,
  booms — "what makes playback feel right") are emitted at their call sites
  but dropped. The recorder therefore can't "tee an outbox"; it must be fed
  directly. Cleanest: give `net_send_event` (or a thin sibling both call) a
  recorder sink that runs regardless of session, so every existing event
  call site feeds the replay whether or not netplay is live. This is real
  wiring, not a footnote — EVENTS records depend on it.
- A new game stamps a random `run_id` into `GameState` (append-only field,
  version bump) and the replay header; on resume the recorder reads the
  save's `run_id` and, if it matches `current.nrp`'s, appends to that file
  starting with a resume-seam keyframe instead of starting fresh — so
  exit→continue stays one file. (`run_id` rides `serialize_game`, so it also
  appears in net snapshots — harmless, but `net_apply_state` must tolerate
  the new field per the savegame append-only convention.)
Run end (game over and `~GLGame`) patches the header and rotates
current→recent (+ the best check) per the rules above.
**Exit**: headless driver plays a scripted run; asserts a completed run
rotates into `recent.nrp` with header fields matching the run
(score/generation/duration), a higher-scoring second run promotes
`best.nrp`, a cheat-flagged run does not, an abandoned run keeps a resumable
`current.nrp` (and a subsequent NEW GAME rotates it into `recent.nrp`), and
an exit-to-menu-then-CONTINUE of the same save yields a SINGLE continuous
file (one `run_id`, a seam keyframe at the resume point, gens from before
and after the exit both present) whose patched header score reflects the
whole run.

### R2 — playback ✅ (landed on this branch)
Implementation notes: `NetMode` gained the distinct `NetReplay` value;
playback boots through `GLGame::start_replay_playback(path)` (bootstraps
from the file's first keyframe exactly like the lobby's client bootstrap,
declining unreadable/older files with a log line) and runs
`tick_net_client` with `tick_replay_poll` standing in for the transport
poll. The predicted apply-path forks all got mode gates: local-ship pose
blend/warp-snap/client-owned bullets (`is_local`/`local_ship` now require
NetClient), the black-hole/pulsar force mirrors, the claim/report tail, the
cheat-flag poke, and spectate arming. One fork the plan missed: netplay
never grows the player roster from snapshots (both players exist from the
lobby), so a mid-run player-2 join was invisible — `net_apply_state` now
adds a ghost `GLCar` in NetReplay when a snapshot carries more players,
and split-screen engages at the recorded join moment. The header gained a
`save_version` field (the pad at offset 50) so future builds can parse old
files' GameState bytes. Input is swallowed (Esc exits, P pauses, `=`/`-`
halve/double speed 0.25x–4x, no cheat flag); the world freezes at the last
record rather than extrapolating into an invented future; watching writes
nothing (no high score, no save, no achievements — ghosts carry
`is_local_player=false`). Dev/test entry until R3:
`NEWTONIA_REPLAY_PLAY=<path|current|recent|best|last>`. Exit criteria
enforced by `test/e2e/replay_playback.sh`, all green headless.
**Effect fidelity**: snapshots carry projectiles but not the flash-class
visuals (online those ride MSG_LANCE/MSG_SHOCK echoes or are host-local),
so REC_EFFECT records (kind 4) capture lance pulses and shock arcs in the
exact MSG wire encodings — played back through the same
`net_receive_lance_pulse`/`net_receive_shock_pulse` functions the net
client uses, sounds included — plus nova/giga shockwave rings with their
`Shockwave` parameters. Mint sites push to always-on `Ship::replay_*`
outboxes (the `net_*` twins stay gated on `net_report_shots`, wire
behaviour untouched) drained once per tick. God mode needed nothing new:
its aura/music/warn-tics key off the restored GodMode weapon's state and
its bullets ride the ordinary records.
`GLGame` gains a `NetReplay` mode: `tick_net_client`'s apply/extrapolate
path fed by a file reader instead of the transport; no INPUT sending, no
local authoritative ship — every ship is a remote-style ghost. **Playback
honours the recorded player count** (header field): a 1-player replay renders
a single viewport following P1 via the spectator-flow camera; a 2-player
replay reuses GLGame's existing split-screen — the same two viewports the
game rendered while playing — each following its own ghost ship (no
authoritative/local distinction; both are file-driven). Two reuse caveats,
verified in code: (1) `net_apply_state` special-cases the local ship — pose
blended toward the snapshot instead of snapped, and local-ship bullets are
client-owned (wiped on rollover, never applied from snapshots) — so
NetReplay must route EVERY ship, including P1's bullets, through the
remote-snap path. (2) NetReplay must be a DISTINCT `net_mode_` value, not
NetClient: `net_apply_state`'s generation-rollover block unlocks
achievements (`coop_clear` et al.) when `net_mode_ == NetClient`, and
watching a replay must never earn — ghosts also carry
`is_local_player = false`, keeping every kill-credit path inert. The file
reader tolerates a truncated final record AND a header staler than the
records behind it (both are legal artifacts of the append model — crash
files and un-patched headers). HUD: REPLAY
watermark + elapsed/total time. Controls: pause key pauses, `=`/`-` adjust
playback speed (no cheat flag in replay mode), Esc/back exits to the menu.
No rewind in v1; fast-forward covers seeking at these run lengths.
**Exit**: headless — record a run, play it back, screenshots show the same
world unfolding (ship motion, kills, level clear banner); a 2-player run
plays back in split-screen with both ghosts moving; speed keys and exit
verified.

### R3 — REPLAYS menu ✅ (landed on this branch)
Implementation notes: the row sits below OPTIONS (hidden while no `.nrp`
exists) and the confirm dispatch moved from "last row = options" to
explicit `options_row_index()`/`replays_row_index()` helpers. The list
screen follows the options screen's layout grammar: desktop shows a cursor
row per file with SCORE / LEVEL / date columns, touch reuses the options
band geometry (tap a run to watch it, RETURN TO MENU band exits), and the
shared nav translator gives the controller w/s/confirm/back for free.
`scan_replays()` re-reads the three headers on every menu build and list
open, so the rows always match disk; a file the build can't parse renders
as an unselectable OLDER VERSION row, and a selection whose file changed
underneath (rotated/deleted) rescans instead of crashing. Selecting a row
hands the state to `start_replay_playback` (R2); Esc from playback lands
back on the menu. Exit criteria enforced by `test/e2e/replay_menu.sh`.
Playback gained on-screen controls: touch gets real tap targets (SLOWER |
PAUSE/RESUME | FASTER stacked above the labeled RETURN TO MENU band, the
TapBand one-definition rule), desktop/controller a dim hint line above the
timeline built from the live bindings ("P PAUSE  =/- SPEED  ESC MENU" /
"START PAUSE  B MENU"). Rewind stays out per the v1 decision, but the
format's keyframe seek makes a "jump back 10 s" control a bounded add-on
(restart the reader, apply silently to the target slot) if wanted later.
Menu row REPLAYS (hidden while no `.nrp` exists) → list screen: CURRENT RUN
(`current.nrp`, shown while a resumable run exists), LAST RUN
(`recent.nrp`), ONLINE RUN (`online.nrp`, the most recent online session —
added with R-online), and BEST RUN (`best.nrp`) rows showing score, level
reached, date (a crashed run's row shows its last-patched header score,
which may understate — accepted). Esc/B backs out; touch gets tap bands.
Selecting a row starts R2 playback. Version-mismatched files render as
unselectable "OLDER VERSION" rows.
**Exit**: keyboard, controller (shared nav translator), and touch all drive
list → playback → back-out; verified headless + on-device.

### R-online — online games record too ✅ (landed on this branch, 2026-07-20)

Both roles record into `replays/online.nrp` (see the decision above for the
full model). Implementation shape: the host tees inside
`net_host_send_snapshot`/`net_send_delta` (the built payload bytes ARE the
records; a second builder call per slot would corrupt the shared delta
baseline, so `replay_record_slot` stays offline-only) and keeps the cadence
through a rejoinable disconnect (sends skipped, records kept). The client
tees the reassembled keyframes + every arriving delta in `net_client_poll`
(before its stale gates — those guard the live timeline, not the file) and
opens the file with a self-built keyframe of the just-bootstrapped replica
(doubling as the rejoin resume seam; `run_id` arrives in every snapshot).
Remote effects tee at the MSG_LANCE/MSG_SHOCK/MSG_SHOT receive sites via
`replay_record_polyline`/`replay_record_shot`; local ones drain from the
`Ship::replay_*` outboxes in both roles' ticks. Received EV_* events tee
with the send tee's skip list; EV_GENERATION_START doubles as the client's
level-boundary flush. Lifecycle: `online.nrp` is the REPLAYS menu's ONLINE
RUN row (4th slot) and never rotates — game-over latches on either role
finalize + best-check it in place, abandons leave a clean-patched file
(still listed, still watchable, and the rejoin-resume target), and the
next online session overwrites it after best-checking a cleanly-closed
leftover. `NEWTONIA_REPLAY_PLAY=online` plays the file directly.
**Exit criteria — all verified headless (`test/e2e/replay_online.sh`)**:
both sides bank identical record counts in S1; the host's file grows
through a SIGKILLed peer; the relaunched joiner resume-appends across
rejoin ("replay: resuming recording"); clean abandons patch both headers;
both files play back split-screen with the joiner's timeline shorter by
exactly the compressed disconnect gap.

### R4 — leaderboard hooks (with the leaderboard project)
Score submission attaches the finalized replay blob; server stores it;
leaderboard rows link to watchable replays (download → R2 playback).
Seasons bucket by sim-affecting release. Submission format carries a
reserved verification field so R5 can slot in without a format break.

### R5 — deferred: input-log verification
Only if forged submissions appear: sim-RNG split (~78 `rand()` sites),
step-indexed input capture, canonical-build re-simulation server-side.
Design notes live in the session log (2026-07-17); nothing reserved beyond
R4's field.

## Open questions (decide before R1 lands)

- ~~Recorder overhead target on mobile~~ **RESOLVED by field measurement
  (Android, 2026-07-20)**: recording at generation 9 with 113 asteroids,
  the perf logger stayed silent during play (accumulated tick time 5–27 ms
  per second even across the level-march rebuilds) and Glenn reports no
  perceptible hitches. The recorder is below the noise floor on device;
  default-on everywhere stands, `NEWTONIA_REPLAY_DISABLE` remains the
  escape hatch and no Options toggle is needed on this evidence.
- Checkpoint-flush latency: the append model bounds every lifecycle flush to
  roughly one level's worth of records by construction, so the open item is
  confirmation, not design: profile the bounded append on-device to check it
  (a) doesn't hitch the frame it lands on and (b) on Android completes
  inside the `onPause`/`onStop` budget before the OS suspends the process —
  a flush that doesn't finish loses the records it was appending (the file
  stays valid; the reader drops the truncated tail). The header patch +
  current→recent rotation happen only at game over / in the menu, outside
  any lifecycle budget, so marathon-run file size never meets a de-focus
  window. **Both halves field-verified on Android** (2026-07-20):
  durability — app backgrounded mid-run then killed from the switcher, the
  background flush landed and the replay played back intact after relaunch
  (so the append also fit the `onPause` budget); latency — no perceptible
  hitches through play including level boundaries, where the flushes land.
- **Xbox certification is why the append rule is global, not an
  optimisation.** On Xbox/GDK the recorder flushes through the same
  `focus_lost()` hook the PLM suspend callback already calls
  (`xbox_main.cpp:171-177`: `plm_suspend_callback` → `focus_lost()` →
  `XSuspendResumeAcknowledge`), so the flush runs *before the ack*, inside
  the certified suspend budget (~1 s TCR — formally tested, and the title is
  TERMINATED, not merely hitched, on overrun). A background thread doesn't
  rescue this: once acknowledged the OS suspends the process, so the write
  must COMPLETE before the ack, not outlive it. The bounded append (kept
  tiny by the proactive level-clear/intro checkpoints) is what fits that
  window. Quick Resume (constrained mode) snapshots the whole process RAM,
  so the un-flushed chunk survives a Quick-Resumed suspend even with no disk
  write — only a true termination needs the on-disk copy — but since the
  title can't tell which it will get, the append still runs. Cross-reference
  `xbox/PORT_PLAN.md` (the existing "suspend completes within the time
  budget" verification item).
- Whether a run with zero sim ticks should leave a replay (proposal: no — a
  flush with no new records is a no-op, and a `current.nrp` containing no
  DELTA records is never rotated into `recent`; that covers
  new-game-instant-quit, and a resume-then-instant-quit appends nothing, so
  the existing file — and the run it holds — is left exactly as it was).
- Cap on file size for marathon runs (proposal: none locally; the
  leaderboard submission path can cap/reject server-side).
- ~~Maybe: an "Auto-record replays" toggle on the Options screen~~
  **PARTLY DONE (2026-07-22): the preference exists, the Options row does
  not.** `Preferences::auto_record_replays` (default ON) is INI-backed
  (`auto_record_replays=` in preferences.ini) and gates the recorder at
  game start (`GLGame::replay_start`) — a superset of the
  `NEWTONIA_REPLAY_DISABLE` env hatch, so storage-conscious players can opt
  out by hand-editing the INI, and it doubles as the perf escape hatch. The
  user-facing Options-screen row is deliberately NOT wired yet (see the
  revisit item below) — kept off the menu for now because it's arguably
  clutter for a feature meant to be invisible.

## To revisit

- **Surface `auto_record_replays` on the Options screen.** The preference
  and its recorder gate already exist (above); this is just the UI. Wiring
  is cheap: one `opt_row` in the data-driven Options list (`bool`, ON/OFF
  like friendly-fire) bound to `g_prefs.auto_record_replays`; touch shows
  the shared-options one-row form, P2 rows unaffected. Decide alongside any
  other Options-screen additions so the menu grows in one pass rather than
  one toggle at a time.
- **Extend the mobile-overhead pass to the low end.** The overhead question
  above was resolved on a mid-range Android (2026-07-20); a low-end
  field pass on real hardware (Moto E14 2 GB Go for CPU/RAM/lifecycle, Moto
  G05 eMMC for storage flush) is the belt-and-braces confirmation. The
  device rationale and step-by-step `adb` procedure live in TESTING.md §7
  ("Replay recorder overhead on low-end Android"); run it when the phones
  arrive and record the result there.
