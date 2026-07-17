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
- **Auto-keep two local replays**: `replays/last.nrp` (finalized every time a
  game ends — game over OR abandoned to the menu) and `replays/best.nrp`
  (promoted from last when the run's final score beats best's header score
  AND the game wasn't cheat-flagged; cheat runs still get `last` — useful
  for debugging — but can never become `best`).
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
header:  magic "NWRP" | format version | game version string |
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
- The file streams to disk during play; the header's score/duration/flags
  are back-patched at finalize. A crash mid-run leaves a truncated file
  that still plays to its last intact record (free crash-repro artifact).
- **Resumed games work naturally**: the first record is always a keyframe
  (full world state), exactly like a rejoining net client's bootstrap — a
  replay of a resumed save just starts from that state.

## Milestones

### R1 — recorder
`replay.h/cpp`: a `ReplayRecorder` owned by GLGame (solo, non-cheated-path
agnostic — records always). Hooks: the 10 Hz slot cadence calls the same
keyframe/delta builders `net_host_send_snapshot` uses but sinks to a
`Save::FileStream` instead of the session; the EV outbox tees per slot.
Finalize from game over and `~GLGame`; rotate last→best per the rules above.
The snapshot builders move behind a small seam so host-send and recorder
share them (they must never fork).
**Exit**: headless driver plays a scripted run; asserts `last.nrp` exists,
header fields match the run (score/generation/duration), a higher-scoring
second run promotes `best.nrp`, a cheat-flagged run does not, an abandoned
run still writes `last`.

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
  default on Android/iOS/web.
- Whether `last.nrp` should survive an immediate quit-at-menu with zero
  sim ticks (proposal: no — reuse the save_dirty_ idea: don't finalize a
  recording with no delta records).
- Cap on file size for marathon runs (proposal: none locally; the
  leaderboard submission path can cap/reject server-side).
