# 4-Player Mode — Implementation Plan

Status: **Phase A COMPLETE** (up to 4 local players live; P3/P4 join by
controller). **Phase B COMPLETE** — B1–B7 all merged: the
`NET_PLAYER_CAP` flip landed in #445, followed by the seat-HUD (#446)
and rejoin-by-identity (#447) follow-ups; online rooms hold up to 4
players. Shipping gate — **SATISFIED 2026-08-14**: the production
signal worker must carry the B3 multi-join protocol before any B7
client build reaches players, and the `v1.53.0` tag (2026-08-14, the
first tag after B3 merged) deployed it. **§5 Outstanding work** is now
empty of open items — its entries stay as the record of what each was
and how it closed.

Goal: raise the local co-op cap from 2 to 4 players (split-screen, desktop +
controllers), and lay the groundwork for — but not yet ship — 4-player online.
The plan is split into **Phase A (local 4P)**, which is self-contained and
shippable, and **Phase B (online 4P)**, a much larger protocol/infrastructure
effort outlined here so Phase A makes no decision that blocks it.

Touch platforms stay single-local-player (they already compile out the join
paths); Phase B raised the ONLINE cap to 4, so a touch device seats up to
three remote friends even though it never gains local seats.

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
| Netplay (Phase B) | ALL LANDED — B1 moved per-peer state into `NetPeer`; B2 made WELCOME/snapshots/headers seat-keyed; B3 made the signal worker host+3-joiners with per-jid tags/offers/identity; B4 host fan-out + waiting-room lobby; B5 per-seat resume/rejoin + spectate cycling; B6 N-instance e2e; B7 cap flip + seat HUD |

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

**D9 — Options screen stays a flat list. RESOLVED 2026-08-15: it holds,
no fallback needed.** Adding P3/P4 sensitivity/smoothing/camera rows takes
the desktop list from 9 to 15 rows; the band compresses via
`opt_row_center(row, n, …)` (menu.cpp:171) automatically. Verified by
looking (`shots/options.shot`, §5 O5): 15 rows is comfortable at 1280×720,
1280×800, 1024×768 and even 800×600. The fallback — collapsing the three
per-player kinds into one row per player cycling a sub-value — is not
needed and is dropped. Touch table is untouched (single local player).

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
  **Measured 2026-08-15 (§5 O6): it does not hurt — the capture copies the
  VIEWPORT, so four quarter-viewports move the same pixels as one full
  one, and the worst case costs 1.8× the 1P lens (1.57× at 1080p), not 4×.
  The round-robin gate is dropped.**

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
  existing rule and stays). **Amended 2026-08-11**: "first fallen" meant
  first in the list, i.e. the lowest seat, however recently they fell — fine
  at 2P where there was only ever one partner down, arbitrary at 3-4P. It now
  revives whoever has been out LONGEST (`Ship::out_order()`, stamped in
  `step()` at the fully-out transition so every path that empties a seat is
  covered, not just the fatal blow). One per pickup and one pickup in the
  world at a time are unchanged.
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
  (cosmetic; closed post-B7 — `add_remote_player` routes through
  `make_seat_ship`, so a host's remote hulls carry their seat's
  shape/tint like the client and replay paths always did, and a mixed
  LAN+relay room renders the LAN peer's claimed name via the per-peer
  offline carve-out, `net_id_ctx_for_seat`); and IN-GAME loss handling
  is still front-peer-only —
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
  seat). Post-B7 follow-ups closed two of the limits left open here:
  **rejoin is by identity now** — the door still pre-picks the lowest
  parked seat, but a seat resolver installed on the adoption session
  (`NetSession::set_seat_resolver`) matches the rejoiner's HELLO claim
  (name AND platform) against the parked peers' remembered identities
  between the HELLO parse and the WELCOME, so each pilot lands back on
  their OWN hull whatever order they return in (ambiguous/nameless
  claims keep the door's pick; the Ready handler moves the whole
  adoption onto the resolved peer, or drops-and-reoffers if that seat
  closed mid-handshake); and the **eaten-offer watchdog** — the door's
  unaddressed offer is consumed on delivery by whichever joiner socket
  is oldest at that instant, which right after an N>1 rejoin completes
  can be the just-seated client's socket draining toward close (2P
  never re-arms into that window); an offer unanswered for 6 s with no
  handshake in flight is re-pushed, un-wedging the next rejoiner.
  Verified by `test/e2e/nseat_swap.sh` (seats 3+4 SIGKILLed together,
  pilots return in the OTHER order, each lands on their own hull).
  Still open: a rejoiner whose adoption transport flaps waits out the
  full ICE timeout before the door re-arms (the fast PeerJoin re-offer
  is N=1-only — a fresh jid cannot name a seat).
- **B6 — Multi-instance e2e**. LANDED: lib.sh gains the N-seat room
  helpers (`room_setup N [host-env…]` assembles host + N-1 relay joiners
  through the waiting room and waits for seats/auto-start/bootstraps;
  `room_alive`/`room_kill_all`/`fly_all`/`new_window_since`; `launch`
  takes per-instance env so a test hook can arm ONLY the host). Four
  SEATS-parameterized drivers (3|4, default 4): `nseat.sh` connect/play,
  `nseat_rejoin.sh` (seat 3 SIGKILLed — a middle seat at 4, so the
  roster goes non-contiguous — play-on unpaused, rejoin back onto seat
  3), `nseat_gameover.sh` (the "all" kill hook empties every seat;
  every client must OBSERVE the game over via replicated lives, hold
  the card, and shrug off the host's post-game-over departure), and
  `nseat_soak.sh` (per-gen liveness on every instance through the
  hazard/mini-station/black-hole generations, no-drop asserts).
  `fourseat.sh` runs the functional three at SEATS=4 (SOAK=1 appends
  the soak); `threeseat.sh`/`threeseat_rejoin.sh` are now SEATS=3
  wrappers keeping their OK-line contracts. CI stays 2-instance
  (runner cost); the N-instance suite runs locally/on-demand like
  gensoak. All verified at 3 and 4 seats against a wrangler-dev relay.
- **B7 — Flip `NET_PLAYER_CAP` to MAX_PLAYERS**. LANDED: the constant
  flips (net_seat_cap() now answers 4 with no env override — the
  `NEWTONIA_NET_TEST_SEATS` hook stays for pinning e2e drivers to a
  seat count), and the touch waiting room gains its deferred
  tap-to-start band (`kStartBand`, the touch twin of the desktop
  "ENTER - START GAME" row — drawn steady, tappable only once a peer is
  seated, sitting clear of the share band's padded box). The flip's
  adversarial review caught pairwise residue the cap made live, all
  fixed with it: the seven GLGame-resolved achievement relays
  (station/mini-station/shield_ram) were still `remote_player()`-keyed —
  a non-back seat's earn was dropped and the broadcast reply unlocked on
  every bystander client — now targeted via `net_peer_for_ship` +
  `net_send_event_to` (PB-D4), with the lance thud dedup relaying to
  the non-firing peers; the host's adopt-smoothing drain looped over
  every peer hull (back()-only left a non-back seat's post-blackout
  catch-up undrained forever); the LAN door closes on any live classic
  `session_` (the `!waiting_room()` guard went dead at cap 4, letting a
  LAN exchange overwrite a degraded-flow session mid-ICE); the touch
  start band's hit-test is touch-gated (desktop clicks on the room code
  must not start the game); joiner-side seat labels ("YOU ARE PLAYER
  N", the local badge fallback) read the WELCOME seat; the lobby's
  signal-level PeerLeave flash is branch-scoped. Known limits at landing: the
  host had no per-seat kick/manage UI — built since, see §5 O3. (The
  single-peer badge/score-row and
  DISCONNECTED-notice seams flagged here at landing were closed in the
  follow-up seat-HUD PR: `Overlay::net_badges` draws one row per
  occupied seat in seat order with every pilot's live score, the
  loss notices count/name lost seats — including a play-on partial-loss
  header with the room code, which the all-lost path had and play-on
  lacked — and a new `MSG_PEER_IDENT` relay lets a client's HUD name
  the OTHER clients: the host shares each seat's badge identity at each
  peer's first INPUT and on attestation changes, deliberately not a
  PROTO bump — an older receiver ignores it and keeps role labels,
  the identity-append precedent.) SHIPPING GATE — **SATISFIED by v1.53.0
  (2026-08-14)**: the production signal worker had to carry the B3
  multi-join protocol — deployed by a release tag — before any B7 client
  build reached players; a pre-multi-join worker degrades every room to
  the classic single pair (2P, with the LAN-door overwrite above now
  guarded).

Cost check against the 3–5× Phase A estimate: B1 and B2 are each roughly
an A3-sized PR; B3 is a worker-only project with its own test rig; B4 is
the big one; B5–B6 are bounded by decisions already made. The estimate
stands.

## 5. Outstanding work

Everything left after B7 + follow-ups (#445–#448), written to be
picked up cold. Ordered by urgency. Inventory taken 2026-08-11,
refreshed 2026-08-15.

**Nothing open.** Every entry here is closed; they stay below for the
record.

### O1 — Ship it: cut the next release tag — DONE (v1.53.0, 2026-08-14)

Was: the production signal worker (`newtonia-signal`) still ran pre-B3
code. B3 (#440) merged 2026-08-10 and auto-deployed only the BETA
worker (master-push → beta is the pipeline's rule), while the newest
tag v1.52.0 was cut 2026-08-08, before B3. Against a pre-multi-join
worker every room degrades to the classic single pair — no jids means
no waiting room — so a B7-flipped client must not reach players first.

Closed by the `v1.53.0` tag (2026-08-14), which contains #440 and whose
deploy-signal run shipped the top-level (non-beta) config. The gate
worked as designed: the worker deploys in minutes while the same tag's
store builds (Steam beta branch, TestFlight, Play) take far longer to
reach anyone.

### O2 — `coop_clear` portal text says "2-player mode" — DONE (2026-08-15)

Was: the achievement unlocks for any clear with ≥2 players (unchanged —
4P clears count), but the player-facing text in all three stores read
"Clear a level in 2-player mode" (entered 2026-07-26).

Reworded to **"Clear a level in co-op mode"** — a straight "2-player" →
"co-op" substitution — in Steamworks and the Play Console (both live) and
in App Store Connect (**submitted 2026-08-15, in review**: Apple keeps the
copy in the achievement's Achievement Localization section and a live
localization edit needs Add for Review → Submit for Review rather than
going live on Save; it can go standalone, no app version). The two
ACHIEVEMENTS.md tables (§2 ASC copy of record, §5 master list) match what
was entered. No code change — the symbolic id, gating, and point values
are untouched.

### O3 — Host per-seat manage/kick UI — DONE (2026-08-12)

Was: the hosting waiting room showed the roster but offered no way to
remove a peer, so a wedged or unwanted joiner squatted a seat until they
disconnected themselves and the host's only recourse was abandoning the
room.

Built as the online half of the offline seat roster, exactly as the
sketch here proposed — one geometry, one ladder, two contexts:

- **Mid-game**: the pause menu's PLAYERS row now opens for the HOST as
  well as offline (`roster_available()`); remote rows name the pilot and
  carry KICK / BAN instead of the local rebind, and the ADD row is dropped
  (online seats fill from the room, not from this machine).
- **Lobby**: the waiting room's roster rows gained a selection that
  rests at -1 — where confirm still means START GAME, unchanged — and
  move onto a seated peer to kick them.
- Both take **two confirms** (arm, then do): ENTER is also the start key
  and the key that opens the screen, so one stray press must not end
  someone's game. Any roster change disarms.
- `GLGame::net_kick_peer` / `NetLobby::host_kick_selected` share the
  shape: send `EV_KICKED` (PROTO-appended, unknown codes are ignored by
  older clients), pump it out, then drop the session. The event is what
  makes a kick stick — a peer that only saw its transport close would
  come straight back through the rejoin door. Mid-game it then takes the
  ordinary park path, which frees the hull and reopens the seat.
- Client side: terminal, no auto-rejoin (the BYE machinery), with its
  own card — "REMOVED FROM THE GAME", because "the host left" would be a
  lie and would send them back to the room code.

**Ban as a SECOND action** (added on request, then split from the kick on
a second request, 2026-08-13): the row offers both, picked with the same
left/right that cycles a local seat's input — meaningless on a peer row,
so it was free. A **kick** removes them and lets them come back with the
room code, which is the ordinary case: the fix for a joiner stuck at the
wrong seat should not also be a punishment. A **ban** additionally bars
that pilot for the host process's lifetime — `NetLobby::ban_identity`,
keyed on case-folded name + platform, because a jid is minted per socket
and the room code is already in their hands. Enforced at the two points
where a handshake first says WHO answered: the waiting room refuses to
seat a banned Ready, and the mid-game rejoin door drops the adoption and
re-offers, leaving the seat parked for whoever it belongs to. A NAMELESS
peer can be kicked but not banned (nothing to key on, and matching on
platform alone would bar every desktop player). Hosting a NEW room clears
the list; rejoining/resuming an existing one keeps it. KICK is the
default on every row and the highlight resets to it, so the harsher
action is always a deliberate press.

**BAN is only OFFERED where a ban would hold** (2026-08-13): it keys on
name + platform, and a merely claimed name is a self-report the peer can
change on their next handshake, so `net_identity_anonymous()` — the
worker attested the NAME itself, not merely the account — gates the
action. An unattested row reads `KICK`, with no arrows offering a second
action the game cannot deliver.

**ALLOW ANONYMOUS PLAYERS YES/NO** (same request): the other side of that
predicate, as a row on both screens — the waiting room's list and the
in-game roster — writing one preference (`Preferences::allow_anonymous`,
saved on change, default YES = the behaviour that always existed). NO
refuses every unattested peer at the same two points the ban is enforced.
Both are the same point now: `NetLobby::admit_verdict`, installed on the
joining session as its `NetSession::set_admit_check` and answered INSIDE
the handshake, before the WELCOME. That placement buys two things the
after-the-fact checks could not. A refusal reaches the peer as a REJECT
reason (`RejectAnonymous`/`RejectBanned`) instead of a closed transport,
which the joiner could only read as "could not connect" — it was telling
refused players their firewall was to blame. And a verdict still in
flight HOLDS the welcome (`AdmitWait`, up to `ADMIT_WAIT_MS`) rather than
losing a race: the worker's attestation is async and usually lands AFTER
the handshake it describes, so judging on arrival refuses attested
players for connecting too fast. The waiting room had a grace timer of
its own for that; the rejoin door had none and assumed its immediate
re-offer would win the race the first attempt had just lost.
Strict by design — a room of desktop builds attests nobody, so turning it
off there closes the room to everyone.

Verified against a local relay by a driver per action: `nseat_kick.sh`
(mid-game) bans seat 3 — the peer is TOLD, does not rejoin, a fresh
process under its name is refused, and the host and bystander play on —
and `nseat_lobby_kick.sh` (lobby) kicks seat 3 and proves the softer
action is really softer: the same pilot re-enters the room afterwards.
`nseat_anon.sh` sets the policy to NO and shows an attested pilot seated
beside an anonymous one refused. `nseat_kick.sh` and `nseat_anon.sh` both
self-host a FAKE_VERIFY relay, since the plain dev relay attests nobody
and neither BAN nor the admission rule would have anything to act on.
`nseat_lobby_mouse.sh` covers the same screen under a pointer.

### O4 — N>1 rejoin ICE-flap wait (B5 known limit) — DONE (2026-08-15)

A rejoiner whose reconnect attempt half-establishes and dies (network
blip mid-ICE, app relaunched during the handshake) leaves the host
holding a half-open adoption session. At N=1 a `PeerJoin` arriving
while a session already exists proves the joiner re-entered, so the
host drops the corpse and re-offers immediately (glgame.cpp:2891). At
N>1 that inference is unsound — the relay mints a fresh jid per socket,
so a `PeerJoin` could be a DIFFERENT player arriving for another parked
seat, and tearing down an in-flight handshake on that guess would break
a legitimately slow handshake (the exact failure mode the N=1 branch's
comment documents). So at N>1 the corpse only dies at the transport's
~30 s ICE timeout, and because rejoin doors serve one parked seat at a
time (lowest first), rejoiners queued behind the flapped one wait too.
Self-recovering, never a hang — just slow. Fix direction: attach the
rejoiner's IDENTITY to the join event so the host can match it to the
seat whose handshake is stale — the same name+platform matching #447's
seat resolver does at WELCOME time, applied one step earlier.

#### BUILT 2026-08-15 — client-only, as the scoping predicted

Landed as scoped below, with one addition the first e2e run forced (the
LAN guard, third bullet under "The design"). Verified by
`test/e2e/nseat_rejoin_flap.sh` both ways: green on the fix, and on a
control build with `net_host_rejoin_flap_check` stubbed out the same
driver fails — seat 3's corpse lived to the ICE timeout and the rejoiner
gave up before the host ever re-offered. Regression drivers green:
`rejoin`, `nseat_rejoin`, `nseat_swap`, `lanrejoin`, `hostresume`,
`lankeep`, `hiccup`.

#### Scoped 2026-08-15 — it is a client-only patch after all

**The premise above is wrong, and the correction is most of the work.**
This entry claimed the fix needs a worker change and a beta-first
deploy. It does not: the host is ALREADY told who is on each joining
socket. The worker broadcasts `{t:"identity", role:"joiner", from:<jid>,
platform, name, verified}` the moment a joiner announces
(`broadcast_identity`, worker.js:880 — `from` added by B3), the client
parses the stamp into `ev.peer` (net_signal.cpp:272), and a rejoining
client announces on its fresh socket for free because it rejoins through
the LOBBY (`NetLobby::send_local_identity`, net_lobby.cpp:1104 — the
auto-rejoin at glgame.cpp:5692 hands the room code to a new `NetLobby`).
The host even banks it already: `net_jid_attested_[ev.peer]`
(glgame.cpp:2898). Nothing is missing on the wire. What is missing is
one correlation, host-side.

**The design.** On `PeerJoin{from:X}` at N>1 while a stale adoption
exists (`hs = net_handshaking_lost_peer()`), resolve X to a seat and
compare:

1. Bank CLAIMED identities by jid too, not only `ev.verified` ones — a
   new `net_jid_claimed_` beside `net_jid_attested_`. Without this the
   fix does nothing in a room of desktop builds, which attest nobody.
2. Hold the join briefly (`pending_join_jid_ = X` + a deadline of ~3 s
   on its own clock) rather than deciding on the spot: the identity
   frame lands a round-trip after the join, and a verified upgrade later
   still. Same shape as `AdmitWait`, same reason.
3. When X's identity is known, compare it against the identity of the
   socket already mid-handshake — **pilots, not seats.** The obvious
   test (does X resolve to the adoption's seat?) is wrong: the door
   always offers the LOWEST parked seat but the WELCOME re-maps the
   session onto whichever seat the claim matches (#447), so with two
   seats parked and the higher seat's pilot answering the lower seat's
   offer — nseat_swap's shape — the lower seat's own pilot rejoining
   would resolve to the adoption's seat and tear down a healthy
   exchange belonging to someone else. One person cannot be two
   joiners, so comparing pilots sidesteps the re-map entirely. Then
   drop the corpse and re-offer, **only if `X != hs->jid`.** The
   jid guard is the one that is easy to miss and fatal without: an
   attestation for the LIVE handshake's own socket arrives late and
   routinely and names exactly this pilot, so without it the branch
   would tear down the healthy exchange that attestation describes — the "AGES to reconnect"
   regression the N=1 branch's comment is a monument to.
   **Added after the first e2e run: the adoption must HAVE a jid.** Both
   doors open on every loss (the LAN beacon comes up beside the room) and
   a rejoining pilot races both roads at once; a LAN adoption carries no
   jid, and an empty jid compares unequal to every real one. Without the
   extra test, the same pilot's relay join would tear down their own
   healthy LAN handshake. A relay join is evidence about relay adoptions
   only.
4. Anything else — no identity, a nameless peer, an ambiguous match
   (the resolver returns 0 for two parked seats with the same name), or
   a different seat's pilot — changes nothing and waits out the ICE
   timeout exactly as today. The fix can only make this faster, never
   slower, which is what makes it safe to land without field proof.

**Matching on a CLAIM is correct here, and the contrast with BAN is the
argument.** A ban keys on attested identity only, because a ban DENIES
someone and a claimed name is a self-report. This decision denies
nobody: it tears down a half-open handshake on a seat that is already
lost, and the worst a spoofer achieves is restarting an honest
rejoiner's in-flight attempt — which is precisely what the 30 s timeout
does today, unprompted. Attestation would buy nothing and would switch
the fix off in every unattested room. Say so in the code; a reviewer
will (rightly) ask why this one takes a claim.

**What it does not fix.** Rejoiners queued behind a legitimately slow
handshake still wait — the door serves one parked seat at a time. That
is a separate, larger change, and it too is unblocked: the worker has
supported **addressed offers** (`{t:"offer", sdp, to}`, worker.js:1026)
since B3, and the lobby's waiting room already mints one transport per
joining jid and offers it addressed (net_lobby.cpp:1458). The in-game
rejoin door is the last unaddressed consumer (glgame.cpp:3177), so
parallel doors are a port of a proven pattern, not an invention — the
"the relay offer is a single unaddressed slot" rationale in
`net_door_peer`'s comment (glgame.h:566) is stale. Worth doing only if
queueing is ever observed to bite; the flap case above is the one with a
field story.

**Tests.** A new `nseat_rejoin_flap.sh` (4 seats): kill seat 3's client,
let the door offer, and have the rejoiner answer and then die inside the
handshake — a client-side test hook (`NEWTONIA_NET_TEST_FLAP=1`, exit
right after the answer goes out) is the only way to hit that window
deterministically. Assert the second attempt seats in well under 30 s
and that the host logs the new drop line. Then the negative, which
matters more: a DIFFERENT pilot joining while seat 3's handshake is in
flight must leave it alone — that is the regression this branch could
plausibly cause. Existing drivers that must stay green: `rejoin.sh`,
`nseat_rejoin.sh`, `hostresume.sh`, `hiccup.sh`, `turnexpiry.sh`,
`lankeep.sh`.

**Cost.** One focused PR: ~60-100 lines across glgame.cpp/.h, one e2e
driver, one test hook, doc updates. No worker change, no PROTO bump, no
deploy dependency, no beta-first sequencing — the constraints that made
this entry look expensive are gone.

### O5 — Options layout verdict (D9, decide-by-looking) — DONE (2026-08-15)

Verdict: **the flat 15-row list is fine; D9's fallback is dropped.**

Phase A shipped the simple thing: the desktop options screen is a flat
15-row list (P1–P4 × sensitivity/smoothing/camera, plus the shared
rows), compressed automatically by `opt_row_center`. Rendered through the
shots harness (`shots/options.shot` — attract → OPTIONS, added for this)
at 1280×720, the Deck's 1280×800, 1024×768 (4:3) and 800×600. It is not
cramped anywhere: 15 rows put ~50 virtual units of pitch under a size-12
row, and the list still clears the BACK TO MENU band with air to spare.
800×600 is the practical floor and reads fine there too.

Two things worth knowing before the next row is added:

- **Window size doesn't enter into it.** Landscape pins
  `Typer::scaled_window_height` to 800 and grows the *width* with aspect
  (typer.cpp:87-96), so the row band is a fixed 350..-400 in virtual
  units at every resolution. Pixel size only scales the whole screen —
  there is no size at which the vertical layout gets tighter, which is
  why the verdict generalises from four renders. Row COUNT is the only
  vertical variable.
- **The tight axis is horizontal, and it is the name column.** The
  longest name today, `LEADERBOARD  UPLOAD`, ends 16 virtual units short
  of the step column's opening bracket — two thirds of a glyph cell. It
  reads fine, but a 20-glyph name would collide, and no window size would
  show you: the columns are fixed constants. Noted in menu.cpp beside
  them, next to the touch layout's equivalent note.

The 15-row case only appears on a netplay build (`net_board_available()`
gates the LEADERBOARD UPLOAD row); the netless list is 14. These renders
forced the row on so the worst case was the one judged.

### O6 — 4P performance budget — DONE (2026-08-15), no action needed

Verdict: **4P costs about 2× a 1P frame, not 4×, and WarpPass does not
multiply by viewport count. A3's round-robin capture gating is ruled
out — it would be the wrong fix.**

Measured with the shots harness (it runs a real render loop, one draw
per 16 ms sim step) plus `NEWTONIA_PERF_ALWAYS=1`, a new hook that drops
`perf_report`'s 55 fps gate so the breakdown prints when both
configurations are fast (TESTING.md). Per-frame ms = the line's ms/sec
divided by its fps. 1280×720 unless noted:

| Scene | 1P | 4P | 4P/1P |
|---|---|---|---|
| Natural gen 6 (68 asteroids) | 6.4 ms (157 fps) | 11.8 ms (85 fps) | 1.85× |
| Natural gen 14 (174 asteroids) | 8.5 ms (118 fps) | 16.9 ms (59 fps) | 2.00× |
| Synthetic, no lens on screen | 2.1 ms (475 fps) | 4.7 ms (211 fps) | 2.25× |
| Synthetic, lens on every screen | 4.5 ms (222 fps) | 8.9 ms (112 fps) | 1.98× |
| — of which the lens pass | 2.60 ms | 4.69 ms | 1.80× |
| — the lens pass at 1920×1080 | 4.81 ms | 7.56 ms | 1.57× |

**The four-captures-per-frame worry is real in count and false in cost.**
`WarpPass::capture` is a `glCopyTexImage2D` of the *viewport* rect
(warp_pass.cpp:291), so four quarter-area viewports move the same pixels
as one full-screen viewport — the pixel budget is constant in player
count. The 1.8× that remains is per-CALL overhead, not per-pixel work,
which is exactly what the resolution row proves: raise the resolution and
the 4P penalty *falls* to 1.57×, because fixed overhead amortises over
more pixels. Gating captures round-robin would therefore stagger a pass
that is already near-constant in viewport count, and buy that with a
visibly stale lens. Dropped.

**Nothing else dominates either.** The base 4× draw lands at ~2×:
per-viewport culling and quarter-area rasterisation pay for half of the
naive cost. The largest single line item at 4P is per-viewport geometry
submission (`objs` + `stars`), which is the "bigger conversation" branch
— but 2× the frame for 4× the players needs no conversation.

Two caveats on the numbers. They come from **llvmpipe** (software
rasterisation under Xvfb), which inflates fill-bound work — the lens
pass — relative to geometry, so "the lens is 60-75% of the frame" is a
software-renderer statement that a real GPU will not reproduce. What
*does* generalise is the structural result (lens cost is sub-linear in
viewport count, because the pixels are constant) and the ~2× geometry
scaling, which is CPU/driver-side. And this box is not weak hardware —
but a software rasteriser at 59 fps for natural gen-14 4P is a
reassuring floor, not a worrying one.

**Follow-up candidate, not opened as work:** the capture uses
`glCopyTexImage2D`, which redefines the texture (reallocating storage)
on every call — four times a frame at 4P. `glCopyTexSubImage2D` into a
texture sized once would keep the pixels and drop the reallocation, and
is the obvious first move if anyone ever profiles this on real hardware
and finds the lens hot. It is a WarpPass improvement at every player
count, which is the point: the lens pass is not a 4P problem.

Reproduce: `shots/lens_stress.shot` (and `sed 's/players 4/players 1/'`
for its twin) — recipe in TESTING.md §"Render cost (1P vs 4P)".

### Accepted as-is (documented so nobody re-opens them by accident)

- **CI stays 2-instance.** The 3/4-seat e2e suites (`nseat.sh`,
  `nseat_rejoin.sh`, `nseat_gameover.sh`, `nseat_soak.sh`,
  `fourseat.sh`) run locally/on-demand against a wrangler-dev relay,
  like gensoak — runner cost, deliberate.
- **Manual clipboard netplay caps at host+1** (PB-D6): structurally
  pairwise, kept as the 2-player rescue path when the worker is
  unreachable.
- **Touch platforms stay single-local-player**; they seat up to three
  remote friends online.
- **P3/P4 ship with no keyboard defaults** (D3): joinable by controller
  only; `p3_*`/`p4_*` INI keys parse for hand-binders. Superseded in part
  2026-08-11 — the pause menu's PLAYERS roster moves any keyboard cluster
  onto any seat at runtime, so 2 keyboard + 2 pad no longer depends on
  join order or on hand-editing the INI. The *defaults* are unchanged:
  P3/P4 still start keyboard-inert.
- **An old build ignores 3+-player saves** (D8) and an old build
  re-saving the INI drops `p3_*`/`p4_*` lines (A1) — standard downgrade
  outcomes, documented in savegame.h.
