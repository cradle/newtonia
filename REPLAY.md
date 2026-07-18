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
- **Auto-keep two local replays**: `replays/last.nrp` (header back-patched
  every time a game ends — game over OR abandoned to the menu; a later resume
  of the same run reopens and continues it, see the run-scoped decision
  below) and `replays/best.nrp` (promoted from last when the run's final
  score beats best's header score AND the game wasn't cheat-flagged; cheat
  runs still get `last` — useful for debugging — but can never become
  `best`).
- **Replays are run-scoped, not session-scoped — resume continues the same
  replay.** A new solo game stamps a random `run_id` into both the savegame
  (`GameState`) and the replay header. Exiting to the menu back-patches
  `last.nrp`'s header but does NOT close the run. On CONTINUE, if the loaded
  save's `run_id` matches `last.nrp`'s, the recorder reopens that file and
  appends — starting with a fresh keyframe as a resume seam (sim time
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
- **Solo games only in v1.** Online games are excluded (client-authoritative
  positions mean the host's stream isn't the whole truth, and saves are
  already NetOff-gated; same gate applies here).
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
- **Storage model: buffer in RAM, flush at checkpoints** (all platforms, not
  streamed per slot). Records accumulate in an in-memory `Save::MemStream`
  (the type netplay already builds snapshots into) during play; the whole
  buffer is written to `replays/*.nrp` — header back-patched — at each
  checkpoint: game-over/abandon (finalize), AND the same pause / background /
  focus-loss points the savegame already auto-saves at. At a few MB per long
  run the RAM cost is negligible, and this eliminates any 10 Hz disk I/O on
  the game thread — the mobile-overhead risk. This is what web already does
  implicitly (its pref path IS MEMFS; syncfs→IndexedDB fires only at flush),
  so native now matches web instead of being the odd one out. Each flush is a
  whole-buffer rewrite, so the on-disk file is always valid and complete —
  never a half-written trailing record. Durability equals the savegame's: a
  hard crash loses only play since the last checkpoint (the background flush
  covers Android's silent kill of a suspended process); the flushed file
  stays a free crash-repro artifact up to that point.
- **Resumed games work naturally**: the first record is always a keyframe
  (full world state), exactly like a rejoining net client's bootstrap. A
  resume *within the same run* (the save's `run_id` still matches
  `last.nrp`'s) appends that keyframe as a seam to the existing file so the
  replay stays continuous across exit→continue; a resume whose `run_id` no
  longer matches (e.g. `last.nrp` was already rotated or belongs to a
  different run) starts a fresh recording from that keyframe.

## Milestones

### R1 — recorder
`replay.h/cpp`: a `ReplayRecorder` owned by GLGame (solo, non-cheated-path
agnostic — records always). Hooks: the 10 Hz slot cadence calls the same
keyframe/delta builders `net_host_send_snapshot` uses but sinks to a
`Save::FileStream` instead of the session; the EV outbox tees per slot.
Finalize from game over and `~GLGame`; rotate last→best per the rules above.
The snapshot builders move behind a small seam so host-send and recorder
share them (they must never fork). A new game stamps a random `run_id` into
`GameState` (append-only field, version bump) and the replay header; on
resume the recorder reads the save's `run_id` and, if it matches
`last.nrp`'s, reopens that file and appends a resume-seam keyframe instead of
starting fresh — so exit→continue stays one file.
**Exit**: headless driver plays a scripted run; asserts `last.nrp` exists,
header fields match the run (score/generation/duration), a higher-scoring
second run promotes `best.nrp`, a cheat-flagged run does not, an abandoned
run still writes `last`, and an exit-to-menu-then-CONTINUE of the same save
yields a SINGLE continuous `last.nrp` (one `run_id`, a seam keyframe at the
resume point, gens from before and after the exit both present) whose header
score reflects the whole run.

### R2 — playback
`GLGame` gains a `NetReplay` mode: `tick_net_client`'s apply/extrapolate
path fed by a file reader instead of the transport; no INPUT sending, no
local authoritative ship — every ship is a remote-style ghost; camera
follows player 1 via the spectator-flow camera. HUD: REPLAY watermark +
elapsed/total time. Controls: pause key pauses, `=`/`-` adjust playback
speed (no cheat flag in replay mode), Esc/back exits to the menu. No rewind
in v1; fast-forward covers seeking at these run lengths.
**Exit**: headless — record a run, play it back, screenshots show the same
world unfolding (ship motion, kills, level clear banner); speed keys and
exit verified.

### R3 — REPLAYS menu
Menu row REPLAYS (hidden while no `.nrp` exists) → list screen: LAST RUN /
BEST RUN rows showing score, level reached, date; Esc/B backs out; touch
gets tap bands. Selecting a row starts R2 playback. Version-mismatched
files render as unselectable "OLDER VERSION" rows.
**Exit**: keyboard, controller (shared nav translator), and touch all drive
list → playback → back-out; verified headless + on-device.

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

- Recorder overhead target on mobile: the delta path is cheap and keyframes
  are 1 Hz, but measure with the perf logger on-device before enabling by
  default on Android/iOS/web. Disk I/O is no longer the concern — the
  buffer-in-RAM + flush-at-checkpoints storage model (see File format) means
  no per-slot writes on the game thread; what's left to measure is the 10 Hz
  serialize/allocation cost (build_save_data + MemStream churn) at HIGH
  generation on a low-end Android device. Reuse the buffers across slots
  rather than reallocating; netplay already runs this exact serialize 10 Hz
  while hosting on mobile, so the ceiling is known-acceptable.
- Checkpoint-flush latency: the buffer→disk write is now a single multi-MB
  blob at finalize / pause / focus-loss, so it must be profiled at a
  marathon-run buffer size (worst case) to confirm it (a) doesn't hitch the
  frame it lands on, and (b) on Android completes inside the `onPause`/
  `onStop` budget before the OS suspends the process — a flush that doesn't
  finish loses the run it was meant to save. If either fails: cap the
  per-flush size by writing only records appended since the last checkpoint
  (high-water mark → append, not full rewrite), and/or move the write to a
  background thread joined before the lifecycle callback returns. Measure
  before relying on the pause/background flush for durability.
- **Xbox certification tightens the above into a hard rule.** If the recorder
  is reused on Xbox/GDK it flushes through the same `focus_lost()` hook the
  PLM suspend callback already calls (`xbox_main.cpp:171-177`:
  `plm_suspend_callback` → `focus_lost()` → `XSuspendResumeAcknowledge`), so
  the flush runs *before the ack*, inside the certified suspend budget (~1 s
  TCR — formally tested, and the title is TERMINATED, not merely hitched, on
  overrun). A background thread doesn't rescue this: once acknowledged the OS
  suspends the process, so any write must COMPLETE before the ack, not
  outlive it. So on Xbox the suspend-path flush must be bounded small:
  append-only-since-last-checkpoint (the whole-buffer rewrite is banned from
  the suspend path), kept tiny by flushing at cheaper in-game checkpoints
  (pause menu open, level clear) so the suspend delta is a handful of
  records. Quick Resume (constrained mode) snapshots the whole process RAM,
  so the in-RAM buffer survives a Quick-Resumed suspend with NO disk flush at
  all — only a true termination needs the on-disk copy — but since the title
  can't tell which it will get, the bounded append is still required.
  Cross-reference `xbox/PORT_PLAN.md` (the existing "suspend completes within
  the time budget" verification item).
- Whether `last.nrp` should survive an immediate quit-at-menu with zero
  sim ticks (proposal: no — reuse the save_dirty_ idea: don't finalize a
  recording with no delta records).
- Cap on file size for marathon runs (proposal: none locally; the
  leaderboard submission path can cap/reject server-side).
- **Maybe: an "Auto-record replays" toggle on the Options screen** (not
  committed). Default ON — the "always record, never ask" decision stands as
  the default — but a user-facing off switch would double as the perf escape
  hatch from the recorder-overhead question above (replaces the env/pref-only
  fallback with a visible control), and lets storage-conscious players opt
  out. Wiring is cheap: one `opt_row` in the data-driven Options list (`bool`,
  ON/OFF like friendly-fire) backed by a `preferences.h` flag the recorder
  checks at game start; touch shows the shared-options one-row form, P2 rows
  unaffected. Open because it's arguably clutter for a feature that's meant to
  be invisible — decide alongside the mobile-overhead measurement, since the
  two share the same off-switch plumbing.
