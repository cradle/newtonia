# 4-Player Mode — Implementation Plan

Status: **Phase A COMPLETE** (up to 4 local players live; P3/P4 join by
controller). **Phase B underway** — B1 (NetPeer refactor) and B2 (PROTO
25 + savegame v19 flag day) merged; B3 (worker multi-join, PB-D5) in
this PR. Netplay stays 2-player until B7 (`NET_PLAYER_CAP`).

Goal: raise the local co-op cap from 2 to 4 players (split-screen, desktop +
controllers), and lay the groundwork for — but not yet ship — 4-player online.
The plan is split into **Phase A (local 4P)**, which is self-contained and
shippable, and **Phase B (online 4P)**, a much larger protocol/infrastructure
effort outlined here so Phase A makes no decision that blocks it.

Touch platforms stay single-local-player (they already compile out the join
paths); netplay stays 2-player until Phase B.

---

## 1. Where the codebase stands today

The 2-player cap is not one constant — it is ~30 scattered assumptions, but
they cluster into a small number of choke points, and a surprising amount of
the code is already N-player-safe.

**Already generic (loops over `players`, no cap):**
- `all_players_out()` (glgame.cpp:977), `revive_fallen_partner()`
  (glgame.cpp:9178), `release_player_controls()`, `player_index_of()`,
  `is_player_controller()`, `has_free_controller()`
- Keyboard dispatch is a broadcast — every `GLShip` filters against its own
  `KeyBinding` copies (glgame.cpp:9292, glship.cpp:575); controller events are
  likewise broadcast and filtered by `wasMyController` (glgame.cpp:8860)
- `controller_added` assigns a new pad to the first player without one
  (glgame.cpp:1117) — no fixed pad→player table
- Pickup collection, ship–ship collision pairs, high-score save loops
- The save format's player list is variable-length (`uint32 count` + N
  records, savegame.cpp:417); the save-load ctor loops it (glgame.cpp:631)
- The replay format carries `player_count` (replay.h:75) and per-record player
  indices; `best_path_for(player_count)` already slots ≥2 as "co-op"
- `num_x_viewports()`/`num_y_viewports()` literally return `players->size()`
  (glgame.cpp:8040–8050) — the viewport *count* is generic; only the renderer
  behind it is not

**The hardcoded-2 choke points (Phase A must touch all of these):**

| Area | Where the 2 lives |
|---|---|
| Join caps | `players->size() >= 2` at glgame.cpp:1171 (add_player2), :1197 (add_remote_player), :5783 (replay ghost join), :8782/:8817 (pad join), :9437 (Enter join) |
| Prefs | `p1_keys`/`p2_keys` named members (preferences.h:118); `binding_for()`'s two-prefix test (preferences.cpp:145); hand-unrolled scalar load/save pairs (preferences.cpp:217–235, :310–342) |
| Key selection | `set_player_keys`: `player_index == 0 ? p1_keys : p2_keys` (glgame.cpp:108) — save-load gives every player past #1 the P2 keymap (glgame.cpp:635) |
| Controller registry | `active_controllers[2]` in state_manager.h:43, loops `for(i<2)` at state_manager.cpp:79/93/120; `controllers[2]` + `opened < 2` in glut.cpp:56/605; same in xbox_main.cpp:90 |
| Renderer | `draw()` calls `draw_world(front(), true)` + `draw_world(back(), false)` only (glgame.cpp:7984–8027); `setup_viewport(bool primary)` with the portrait `//HACK` flip (glgame.cpp:8280) |
| Viewport math | aspect computed as `window.x() / (window.y()/ny)` — missing `/nx` — at glgame.cpp:8057, 8071, 8267, 8341; `camera_screen_radius` takes only `y_viewports` (glgame.cpp:8165) |
| Minimap/divider | `num_y_viewports() == 2 ? y/6 : y/4` (glgame.cpp:8547); single centre divider + minimap hardcoded at `window/2` (glgame.cpp:8549–8594) |
| HUD | `title_text` gated on `size() < 2`, reads `front()` as p1 (view/overlay.cpp:742); `debug_info` drawn only in `front()`'s viewport (view/overlay.cpp:958) |
| Gameplay stragglers | Nova/blast friendly-fire partner check assumes partner is `front()` (glgame.cpp:1831, :4985); Intro dismissal ORs exactly the p1/p2 shoot bindings (intro.cpp:249) |
| Options menu | `OPT_ROWS_DESKTOP` P1/P2 rows (menu.cpp:73); `[2]` state arrays (menu.h:158); hand-unrolled seed/commit (menu.cpp:286, :1930) |
| Save read cap | `read_count(f, cnt, 2)` for players (savegame.cpp:479); `net_state_sane` rejects >2 (net_session.cpp:250) |
| Netplay (Phase B) | one `NetSession`/transport/assembler/delta-baseline (B1 moved per-peer state into `NetPeer`; B2 made WELCOME/snapshots/headers seat-keyed; B3 made the signal worker host+3-joiners with per-jid tags/offers/identity) — remaining: host fan-out + lobby (B4), per-seat resume (B5); lobby strings "PLAYER 2 …" |

---

## 2. Design decisions

**D1 — `MAX_PLAYERS = 4` constant.** One named constant in glgame.h replaces
every `2` literal in the join/cap/registry sites. `StateManager`, glut.cpp and
xbox_main.cpp size their pad arrays from it (or a mirrored constant if the
include is awkward from glut.cpp).

**D2 — Players are identified by index, not front/back.** Offline, list order
*is* the index (`player_index_of()` already exists). Phase A replaces
front/back with index loops only on the offline/rendering paths; the netplay
front/back sites are Phase B's problem and keep working unchanged because
online stays capped at 2.

**D3 — P3/P4 are controller-first.** The keyboard is out of real estate: P1
owns WASD+arrows+Space/X/Q/C/E/T/V/F1, P2 owns IJKL+`/,.;uoy`+F8, and typical
keyboards ghost beyond 2 simultaneous WASD clusters anyway. So:
- P3/P4 default `PlayerKeys` have **empty key bindings** (a default-constructed
  `KeyBinding` matches nothing — glship.h:114 — so they are keyboard-inert).
  Scalars (sensitivity/smoothing/rotate_view) get normal defaults.
- P3/P4 join **only by controller** (unknown pad presses START/A). The Enter
  key keeps joining P2 exactly as today.
- `p3_*`/`p4_*` INI keys are still parsed/written, so a determined user can
  hand-bind keyboards; we just ship no defaults.

**D4 — Layout: strips for 2, 2×2 grid for 3–4.** Four side-by-side strips on a
16:9 window would be ~4:9 slivers. Instead:
- 1 player → 1×1; 2 players → today's orientation-following strip split
  (unchanged); 3–4 players → **2×2 grid** in either orientation.
- Cell order: P1 top-left, P2 top-right, P3 bottom-left, P4 bottom-right.
- 3 players: the fourth cell hosts the **minimap** (large, in its own quadrant).
- 4 players: minimap shrinks and sits at the centre crosshair of the dividers,
  as the 2P split does today at the divider midpoint.
- `setup_viewport(bool primary)` becomes `setup_viewport(int vp_index)` with a
  single `viewport_rect(i) -> {x,y,w,h}` helper feeding glViewport, the
  divider drawing, and the tap-band geometry (the TapBand rule: one geometry
  definition feeds draw and hit-test). The portrait `//HACK` flip dies —
  `viewport_rect` puts P1 top-left explicitly instead of inverting a boolean.

**D5 — Fix the viewport aspect/audio maths while we're in there.** The four
`window.x() / (window.y()/ny)` sites (missing `/nx`) overestimate half-width
2× in today's landscape split; in a 2×2 grid the culling stays merely
conservative but `camera_screen_radius` would inflate the audible radius for
*every* multiplayer layout. `camera_screen_radius` gains an `x_viewports`
parameter and all four aspect sites divide by `nx`. This slightly shrinks the
audible plateau in the existing landscape 2P split — that is the geometry
being computed *correctly* for the first time; CLAUDE.md's audio note gets
updated to match. (`edge_indicators` at view/overlay.cpp:330 already does it
right and is the reference.)

**D6 — One join function.** The Enter-join at glgame.cpp:9437 is a hand-copy
of `add_player2` that drops `set_black_holes` (a live inconsistency today).
Phase A extracts `add_local_player(SDL_GameController *ctrl, bool with_keys)`:
allocates the `GLCar`, wires missiles/shock/black-holes/friendly-fire, keys by
the new player's index, pushes, `update_presence()`. All three join paths call
it; the black-holes omission is fixed as a side effect.

**D7 — Per-player visual identity** *(amended post-flip: P3 flies the P1
hull, so shapes alternate around the grid — see `make_seat_ship`)*. P2–P4
were all `GLCar` before Phase A. Minimum
viable: a per-index tint applied in `GLCar::draw_ship` (and the same tint on
that player's HUD lives/score block and minimap dot) so four identical cars
are tellable apart. No new models.

**D8 — Save format: raise the read cap, no version bump.** The player list is
already count-prefixed, so writing 3–4 players needs no format change — only
`read_count(f, cnt, 2)` → `MAX_PLAYERS` (savegame.cpp:479). An **older build
loading a 3+-player save** fails `read_count` and ignores the file (treated as
no save) — the standard downgrade outcome, documented in savegame.h. No new
fields → no VERSION bump (stays 17). `net_state_sane`'s `> 2` reject
(net_session.cpp:250) stays at 2 until Phase B, deliberately: it is the online
snapshot validator and online is still 2P.

**D9 — Options screen stays a flat list.** Adding P3/P4 sensitivity/
smoothing/camera rows takes the desktop list from 9 to 15 rows; the band
compresses via `opt_row_center(row, n, …)` (menu.cpp:171) automatically.
Verify legibility with the shots harness; if 15 rows is too tight, the
fallback is collapsing the three per-player kinds into one row per player
cycling a sub-value — but try the simple thing first. Touch table is
untouched (single local player).

**D10 — One co-op leaderboard for all player counts (decided 2026-08-07).**
Every run with `player_count >= 2` competes on the single co-op board; 3P/4P
do not get boards of their own. The client half already works this way —
`best_path_for` slots any ≥2 count into the one best-co-op file
(replay.cpp:415) — but the board Worker does not:
- `validate_submission` rejects anything but 1 or 2 outright
  (`bad-players`, board/src/validate.js:195) → accept `1..MAX_PLAYERS`.
- Scores are keyed by the literal count (`const players = hd.player_count`,
  board/src/worker.js:1171, and the `players` field on the `qualify` /
  `top` / `rank-of` messages), so an accepted 4P run would land on a separate
  `players=4` board. Canonicalise to a **slot** — `players >= 2 ? 2 : 1` — on
  the server for submit and every query (authoritative, robust against any
  client), and send the same slot from the client's `qualify`
  (glgame.cpp:3360). Retention/liveness ("per players count") then keeps
  exactly its two slots.
- The replay header keeps recording the *true* count (display/forensics);
  only the board keying collapses.
- **Deploy order**: board first (beta via master push, then production),
  before any 4P-capable client ships — an old Worker fails safe (refuses the
  upload with `bad-players`, nothing corrupts), but the run's score would
  never chart.

---

## 3. Phase A work plan (local 4-player)

Ordered so each step compiles and is testable on its own. Estimated shape:
~7 PRs.

**Landing strategy (decided 2026-08-09): master-based PRs + a dark-launch
gate — no unfinished behavior in master.** Every PR bases on master and
merges sequentially (a step's dependency arrives through master, never by
stacking PRs). Because tags and Steam beta deploys can be cut from master
at any time, every intermediate merge must be inert: A2–A5 implement
against `MAX_PLAYERS`, but the JOIN caps keep reading a separate
activation constant (`LOCAL_PLAYER_CAP`, = 2 until further notice) so a
third player cannot join a real game before the whole feature is in —
without this, the window between A2 (join caps raised) and A3 (grid
renderer) would let a player join and get no viewport. The
`NEWTONIA_START_PLAYERS` test hook (A6) bypasses the gate under an env
var so headless CI and the shots harness exercise 3–4P throughout.
Phase A then ends with one final one-line PR flipping `LOCAL_PLAYER_CAP`
to `MAX_PLAYERS` once everything is verified — 4P appears atomically.

### A0 — Board: one co-op slot (its own PR, deployed first)
Implements D10, entirely in `board/` plus one client line, shippable ahead of
everything else:
- board/src/validate.js:195: accept `player_count` 1..4.
- board/src/worker.js: normalise the keying slot (`>= 2 → 2`) at submit
  (:1171) and on the `qualify`/`top`/`rank-of` message handlers.
- glgame.cpp:3360: send `min(player_count, 2)` in `qualify`.
- Tests: extend board/test/validate_test.mjs (3 and 4 accepted, keyed as 2)
  and the protocol tests; the deploy-board pipeline gates on them. Deploy
  beta via master push, production with the next tag — before any
  4P-capable client build exists.

### A1 — Identity & preferences plumbing
- glgame.h: `static const int MAX_PLAYERS = 4;`
- preferences.h: replace `p1_keys`/`p2_keys` members with
  `PlayerKeys player_keys[MAX_PLAYERS]` + a `player_keys_for(int)` accessor.
  Ctor sets P2's IJKL overrides at index 1; indices 2–3 get empty bindings
  (D3). Keep the INI **key names** `p1_*`/`p2_*` unchanged and add
  `p3_*`/`p4_*`:
  - `binding_for()` (preferences.cpp:145): parse `pN_` prefix numerically
    (1..MAX_PLAYERS) instead of two strncmp branches.
  - Scalar load branches (preferences.cpp:217–235) and the save blocks
    (preferences.cpp:310–342): convert to loops over the slot array.
  - Legacy global `rotate_view` migration (preferences.cpp:188) seeds all
    four; the downgrade fallback line stays written from P1 only.
  - Downgrade note: an old build re-saving the INI erases `p3_*`/`p4_*` lines
    (whole-file rewrite, preferences.cpp:280). Accepted — same class of loss
    as any unknown key; the `_alt` trick can't protect unknown key families.
- glgame.cpp `set_player_keys` (:108): index into the array; save-load ctor
  (:635) passes the real index `i`, not `is_p1 ? 0 : 1`, so P3/P4 restore
  their own keymaps.
- menu.h/-.cpp: grow the three `[2]` option-state arrays to `[MAX_PLAYERS]`;
  convert seed (menu.cpp:286) and commit (`close_options`, menu.cpp:1930) to
  loops; append P3/P4 rows to `OPT_ROWS_DESKTOP` (leaderboard row stays last —
  `opt_row_count()` depends on it). Recheck the touch name-length note at
  menu.cpp:718.

### A2 — Join paths & controller registry
- Extract `add_local_player` (D6); rewire `add_player2` (:1164), the Enter
  join (:9437 — keeps `set_player_keys(…, 1)` and the P2-only semantics), and
  the pad-join sites (:8782, :8817) to it. All caps become `MAX_PLAYERS`.
- state_manager.h/.cpp: `active_controllers[2]` → `[MAX_PLAYERS]`; the three
  `for(i < 2)` loops follow. Same in glut.cpp (:56, :605) and xbox_main.cpp
  (:90). (Do **not** name any new local `near`/`far` — windows.yml is the only
  CI that catches it.)
- overlay join hints (view/overlay.cpp:741–758): the gate `size() < 2` becomes
  `size() < MAX_PLAYERS`; wording generalises ("PLAYER n: PRESS START TO
  JOIN" when a free pad exists). The block currently suppressed entirely in
  split-screen needs a re-think: show the join hint in a corner of the
  full-window pass (beside `replay_hud`/`net_overlays`, which already reset to
  full-window viewport) rather than per-viewport.
- Pad-join remains blocked online (`net_mode_ == NetOff` guard stays).

### A3 — Renderer: viewport grid
The core of the phase.
- New `GLGame::viewport_rect(int i)` implementing D4 (strips at ≤2, 2×2 at
  3–4). `num_x_viewports()`/`num_y_viewports()` reimplemented on top of it
  (2×2 → nx=ny=2) so every existing `/nx`,`/ny` consumer — overlay layout,
  tap bands, FOV, HUD ortho — keeps working by construction.
- `draw()` (glgame.cpp:7984): replace the front/back branches with
  `for (i, gs in *players) draw_world(gs, i)` on the offline and NetReplay
  paths (replay ghost cap at :5783 raised to MAX_PLAYERS at the same time —
  4P replays then draw for free). Online branch untouched.
- `setup_viewport(int i)` from `viewport_rect`; delete the `//HACK` flip.
- FOV: keep `view_angle() * 0.75f` whenever the viewport's height is halved
  (ny == 2), which now includes the grid.
- Aspect/audio fixes per D5: glgame.cpp:8057, :8071, :8267, :8341 divide by
  nx; `camera_screen_radius(fov, window, x_viewports, y_viewports)`; update
  callers (:8240 and `net_listener_volume`, which passes 1,1) and the
  CLAUDE.md audio paragraph.
- Minimap/dividers (`draw_map`, :8535): draw the divider cross from
  `viewport_rect` edges; minimap placement per D4 (free cell at 3P, centre at
  4P); replace `num_y_viewports() == 2 ? y/6 : y/4` with a rule keyed on
  "am I in a split at all" plus available cell size.
- Overlay: `debug_info` gate (overlay.cpp:958) becomes "viewport index 0";
  everything else already divides by nx/ny and comes along for the ride.
- WarpPass reads the live viewport rect (glgame.cpp:8500) so it is
  grid-correct for free, but up to 4 captures/frame when lenses are on
  multiple screens — measure on the headless driver; if it hurts, gate to
  N captures/frame round-robin (`lens_on_screen` already skips off-screen).

### A4 — Gameplay N-safety sweep
- **Unknown-pad authority (flip blocker, found in A2 review):** an opened pad
  that owns no seat can BACK-exit a live run (glgame.cpp unknown-pad ladder),
  GUIDE-pause, and drive the game-over card/board prompt with RT; menus also
  run every pad through ONE shared nav-stick hysteresis (state.cpp
  `nav_stick_`), so a drifting idle pad can deaden nav for everyone. Harmless
  today (only seatable pads are opened — A2 bounds opens by
  LOCAL_PLAYER_CAP), but the flip makes pads 3/4 openable, and an unseated
  pad must not keep quit/pause/confirm authority. Gate those branches on
  `is_player_controller` (or an explicit "no-seat pads navigate only" rule)
  and make the nav hysteresis per-pad BEFORE the flip PR. **Done in A4**:
  `pad_may_command` gates GUIDE/BACK/game-over-RT (a player's pad, or any
  pad when no player has one — the long-shipped keyboard+couch-pad case),
  and the nav-stick/RT latches are per-pad slots.
- intro.cpp:249: dismissal iterates every player's shoot binding instead of
  the p1/p2 pair.
- ~~Nova/blast partner checks (glgame.cpp:1831, :4985)~~ — moved to Phase B:
  both sites are netplay-only protocol code (client blast vs the host ship),
  exactly-2P until Phase B rewrites peer identity anyway; generalising them
  now would churn code no test can reach.
- Verify the friendly-fire body-collision site (glgame.cpp:7187) iterates all
  player *pairs* (it sits inside the object-pair loop, so it should — confirm
  with a 3P test, don't assume).
- Revive: mechanics are already N-safe (revives the first fallen; multiple
  fallen partners take successive pickups — one in the world at a time is the
  existing rule and stays).
- `all_players_out()`'s seven inlined copies (glgame.cpp:4540, :7714, :8766,
  :8798, :8837, :9002, :9476) are already loops — leave them, or fold into the
  helper opportunistically while touching those files.
- Per-index tint (D7) in GLCar draw + lives/score HUD + minimap dots.

### A5 — Save/replay cap
- savegame.cpp:479: `read_count(f, cnt, MAX_PLAYERS)`; comment the downgrade
  behaviour in savegame.h (old builds ignore 3+-player saves).
- Auto-save/death-save logic (glgame.cpp:7756) is count-agnostic — verify with
  a 3P save/resume cycle that P3 restores with keys, tint, and controller
  re-offer (the pad re-scan at :652 already loops).
- Replays: `Recorder` already takes `player_count`; confirm nothing between
  record and playback truncates indices > 1 (the effect records carry a u8
  player index). `best_path_for` keeps slotting ≥2 as co-op, matching the
  single co-op leaderboard (D10/A0).

### A6 — Tests, shots, docs
- Headless e2e: xdotool can synthesise Enter (P2 join) but not controllers,
  so the `NEWTONIA_START_PLAYERS=N` hook (landed with A2; beta-gated,
  cheat-marked, bypasses LOCAL_PLAYER_CAP) spawns N players at game start. E2e cases: 4P spawn + split renders
  (screenshot), 3P minimap cell, revive with 2 fallen, 4P save/resume, 4P
  game-over latch, 4P replay playback.
- shots/: a 4P split scene for store assets and layout review (also the
  quickest way to iterate the A3 geometry).
- Update CLAUDE.md (players list semantics, audio note per D5) and TESTING.md.

---

## 4. Phase B plan (online 4-player)

Expanded 2026-08-10 from three research inventories (per-peer state, wire
identity, signalling/lobby) — the coarse outline this replaced lives in git
history. Model: still host-authoritative, a star of up to 3 clients (mesh is
a non-starter — the sim is host-only `rand()`). Same landing strategy as
Phase A: every PR bases on master, merges sequentially, and stays inert
behind a new `NET_PLAYER_CAP` (= 2 until the final flip), with an e2e hook
(`NEWTONIA_NET_TEST_SEATS`) bypassing the gate for multi-instance tests.

### Phase B design decisions

**PB-D1 — One `NetPeer` struct; the room/peer split.** Everything the
research classified per-peer moves off GLGame's scalars into a `NetPeer`
owned by a host-side vector: the session (which owns its transport), peer
identity/attestation, the whole input pipeline (`net_last_input_seq_`,
`net_have_input_`, the 7 one-shot counter baselines, `net_held_suppress_`,
dead-man/gap state), the RTT ring, per-peer connected/handshaking/lost
state, and the rejoin-door bookkeeping. Room-scoped and staying on GLGame:
room code/token/signal socket, the snapshot clock (`net_snapshot_timer_`,
`net_slot_`), banners, and the resume ticket. `net_connection_lost_`
splits: per-peer loss state plus room-derived predicates (any-seat-open →
advertise invite; all-peers-lost → the terminal card). The per-tick
`net_event_effect_budget_` becomes per-poll-invocation.

**PB-D2 — Snapshot baseline stays ROOM-level (overrides the old outline).**
The delta baseline `net_known_` is valid for a receiver iff it received
every delta since the keyframe that seeded it. Per-peer baselines would
force N builds per slot and break the replay recorder's shared-builder
invariant (the recorder tees the same bytes). Instead: ONE baseline and
byte-identical broadcast, with the rule that any join/rejoin forces a
GLOBAL keyframe slot (everyone gets the keyframe — a join is rare, the
cost is fine). Rejoin already keyframes today, so this is the current
invariant made explicit.

**PB-D3 — One wire flag-day (PROTO 24→25 + savegame v19), the 21/23
shape.** Everything wire-breaking lands in one bump:
- WELCOME's assigned-id byte carries the seat (2..4); the client STORES it
  (today it validates `!= 2` and discards) and `NetSession::player_id()`
  returns it.
- Snapshot ship records lead with a `u8` seat id (the PROTO-16
  `net_ship_id` enemy pattern), replacing all three positional zips: the
  client state apply, the extras walk (`i + 1 == nplayers` local-detect),
  and the host process-death resume ctor.
- `Save::Player` gains the seat id (savegame v19 append) — fixes
  host-resume seat surgery and gives replays identity in one move.
- The header `player_id` byte becomes meaningful: each side stamps its
  seat (the host currently stamps literal 2 on its own echoes — proof
  nothing reads it), and relayed SHOT/LANCE/SHOCK carry firer attribution
  so a client can no longer assume "the host fired it".
- MSG_BOUNCE / bullet id spaces: partition the per-ship `net_shot_seq`
  mint by seat (top bits) so cross-ship scans stay unambiguous.
- `EV_SHIP_IMPACT` arg widens to seat 1..4 (bits 0-7 already fit; `0x100`
  ting flag unmoved). `EV_REMOTE_SHOT` (superseded since PROTO 17) is
  retired at the same flag day.
- `net_state_sane`'s player cap rises to MAX_PLAYERS.
Old/new builds reject each other through the existing HELLO version gate
("PLAYER N HAS A DIFFERENT VERSION"), as 21/23 did.

**PB-D4 — Host relays client effects.** Today a client's shot/lance/shock
visuals are consumed by the host alone (the only other screen). With N
clients the host re-broadcasts peer A's effects to B and C, excluding A.
The owner-less Ship static outboxes (`net_shot_reports`,
`net_lance_reports`, `net_shock_reports`) gain owner attribution (the
`replay_lance_flashes` pattern — they already carry `const Ship*`);
peer-directed events (EV_PICKUP, EV_ACHIEVEMENT, EV_RAM_BLAST, the
first-INPUT pause/FF resyncs) become targeted sends to the owning peer's
session.

**PB-D5 — Signalling: joiner collection keyed in the WebSocket tags.**
Tags are the only state surviving DO hibernation, so the joiner id lives
in the tag (`["joiner", "j:<n>"]` from a monotonic counter). Per-jid offer
map (`offers[jid] = {sdp, pv, cands[]}`), addressed frames (`{t:"offer",
to}` / `{t:"answer"|"cand", from}`), identity map + per-jid verify
throttles/epochs (the vacant-slot late-verify race gets SIMPLER per-jid),
identity fan-out (a joiner receives host + all sitting joiners; their
attestation reaches everyone), room-full at 4, grace events broadcast to
all joiners. `NetSignal::Event` gains a peer-id field; `send_offer`/
`send_cand` gain a target on the host side; the rtc/web backends are dumb
pipes and recompile untouched. Deploys beta-worker-first (the existing
pipeline); the pv fail-fast covers the host-side frame change, and the
joiner-side frames stay back-compatible.

**PB-D6 — Lobby: waiting room. Manual clipboard stays 2P-only.** RoomHost
accumulates per-joiner pending peers (own transport/offer/handshake each),
shows the roster ("WAITING FOR PLAYERS 2/4", per-peer attestation badges)
and gains a START GAME action — its first-ever confirm input. The joiner
flow is untouched (a joiner is inherently 1:1 with the host; its
Connected screen already waits indefinitely for snapshot #1). The LAN
door mints a fresh offer per accepted TCP connection (an SDP is
single-use) instead of the one-blob slot. The manual clipboard fallback
is structurally pairwise and stays the 2-player rescue path: when the
worker is unreachable, the room caps at host+1.

**PB-D7 — Per-seat resume; play-on policy.** "Host keeps the room,
clients rejoin individually" IS the current model — room code/token/
grace/ticket never referenced the peer, and each client already
auto-rejoins alone. What generalises: per-open-slot rehost doors (relay +
LAN, currently one offer slot each), per-seat park/unpark, and the Ready
reset scoping to its own peer's state. Policy change: a dropped peer
parks their seat and play CONTINUES while other remote peers remain
(pausing three players for one drop is wrong); the 2P behavior — pause
and wait — is kept when the dropped peer was the only one. Spectate
cycles living peers (`remote_player()` becomes an iterator;
`camera_target()` picks the next living ship).

### Phase B work plan

Sequential master-based PRs, inert until the B7 flip:

- **B1 — NetPeer refactor at N=1** (internal only, no wire change): the
  PB-D1 struct with a vector of exactly one; per-peer loss + room
  predicates; outbox owner attribution; the pause-policy seam. The whole
  netplay e2e suite must pass unchanged — this PR is pure motion.
- **B2 — The flag day** (PROTO 25 + savegame v19): everything in PB-D3.
  Still 2 players; behavior identical, bytes different. Gated by the
  loopback selftest + full netplay e2e on both roles.
- **B3 — Worker multi-join** (deployable independently): PB-D5 on the
  worker + signal seam, with the seven new protocol-test families
  (capacity/slot-reopen, per-jid relay isolation, per-jid buffers,
  identity fan-out + per-jid epochs, grace with N, host-close broadcast,
  per-offer pv). Beta worker soak before production. Game still caps 2.
- **B4 — Host fan-out + lobby waiting room**: sessions vector goes real
  (WELCOME assigns 2..4), global-keyframe-on-join rule, host relay of
  client effects (PB-D4), roster + START, LAN multi-offer. Behind
  `NET_PLAYER_CAP = 2`; `NEWTONIA_NET_TEST_SEATS` bypasses for e2e.
  LANDED in two PRs: B4a (#441) turned the host machinery into peer
  loops at N=1; B4b added `net_seat_cap()` (the test-seats override),
  the hosting lobby's waiting room (per-jid pending transports +
  addressed offers/cands, seats reserved at session creation, roster +
  ENTER-START, auto-start when full), the LAN door's fresh-offer re-arm,
  and the multi-session `GLGame` host ctor (`GLGame::NetSeated`).
  The client binds its LOCAL hull by WELCOME seat (rotated to
  `players->back()` at construction so the client-wide back()-is-local
  convention holds on 3-4P rosters; every other hull is stripped of
  bindings and the local-player flag). Verified by
  `test/e2e/threeseat.sh` (3-seat connect/auto-start/play smoke; run
  locally against a wrangler-dev relay). Known B4b limits: touch hosts
  get the roster count line but no tap-to-start band (B7's touch pass);
  a pre-multi-join worker degrades the waiting room to the classic
  single pair (no jids to key on); seats 3+ render the P2 hull model
  (cosmetic); and IN-GAME loss handling is still front-peer-only —
  EV_BYE and the transport-failure watch see only the front peer and
  the rejoin door re-welcomes onto seat 2, so a non-front peer's
  mid-game drop degrades (wrong-peer pause/rejoin, no crash) until
  B5's per-seat resume.
- **B5 — Per-seat resume/rejoin + spectate cycling** (PB-D7). LANDED:
  per-peer loss detection (RX watchdog + transport watch + EV_BYE by
  sender, per seat), per-seat park (NetPeer::parked; the hull freezes
  shielded) with the play-on policy — the room pauses only when the LAST
  live remote drops (net_rejoin_parked_ is now the room-pause latch) —
  and the rejoin doors serve the LOWEST parked seat first (serialized on
  purpose: the relay offer is one unaddressed slot, so simultaneous
  rejoiners would scramble; the second waits its turn). Identity events
  fold by jid across the roster; pings/RTT are per peer. Spectate
  cycles: spectate_target() follows the first living other player and
  advances as they fall. The 2P fast-loss-detect (PeerJoin while
  healthy) stays N=1-only — a rejoiner's fresh jid can't name a seat at
  N>1, so the watchdogs carry it. A doorless loss is terminal only when
  ALL peers are gone. "player N lost/rejoined" log strings keep the 2P
  grep contract at seat 2. Verified by `test/e2e/threeseat_rejoin.sh`
  (SIGKILL seat 3 mid-game → play-on unpaused → relaunch rejoins seat 3)
  plus the unchanged 2P suite. Post-review hardening: paused-tick
  watchdog baselines, the parked-shield re-assert, held-input suppress
  and the pause/BYE/cosmetic-event/touch-exit gates all generalized off
  the front peer (play-on must keep serving the healthy seats); the
  spectate camera skips parked hulls at N>1; a relay adoption at N>1
  closes the still-beaconing LAN door (its blob was minted for that
  seat). Still open for B6/B7: a rejoiner is seated by
  lowest-parked-seat, not identity — two simultaneous drops can swap
  hulls if they rejoin in the other order; and at N>1 a rejoiner whose
  adoption transport flaps waits out the full ICE timeout before the
  door re-arms (the fast PeerJoin re-offer is N=1-only — a fresh jid
  cannot name a seat).
- **B6 — Multi-instance e2e**: extend test/e2e's lib to N joiner
  instances; 3-and-4-seat connect/play/drop/rejoin/game-over suites; a
  soak. CI stays 2-instance (runner cost); the N-instance suite runs
  locally/on-demand like gensoak.
- **B7 — Flip `NET_PLAYER_CAP` to MAX_PLAYERS** once B1–B6 are verified;
  production worker deploy (tag) precedes shipping any B7 client build.

Cost check against the 3–5× Phase A estimate: B1 and B2 are each roughly
an A3-sized PR; B3 is a worker-only project with its own test rig; B4 is
the big one; B5–B6 are bounded by decisions already made. The estimate
stands.

## 5. Open questions

- **Achievements**: `coop_clear` currently means "≥2 players" — presumably
  unchanged (4P clears still count). Confirm no achievement text says "two".
- **Options layout**: flat 15-row list vs per-player sub-rows (D9 fallback) —
  decide after seeing the shot.
- **Performance budget**: 4× world draws + up to 4 WarpPass captures on
  low-end desktop — measure before optimising.
