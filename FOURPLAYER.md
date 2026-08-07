# 4-Player Mode — Implementation Plan

Status: **plan only — no implementation yet.**

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
  (glgame.cpp:9116), `release_player_controls()`, `player_index_of()`,
  `is_player_controller()`, `has_free_controller()`
- Keyboard dispatch is a broadcast — every `GLShip` filters against its own
  `KeyBinding` copies (glgame.cpp:9214, glship.cpp:575); controller events are
  likewise broadcast and filtered by `wasMyController` (glgame.cpp:8810)
- `controller_added` assigns a new pad to the first player without one
  (glgame.cpp:1112) — no fixed pad→player table
- Pickup collection, ship–ship collision pairs, high-score save loops
- The save format's player list is variable-length (`uint32 count` + N
  records, savegame.cpp:417); the save-load ctor loops it (glgame.cpp:632)
- The replay format carries `player_count` (replay.h:75) and per-record player
  indices; `best_path_for(player_count)` already slots ≥2 as "co-op"
- `num_x_viewports()`/`num_y_viewports()` literally return `players->size()`
  (glgame.cpp:8035–8045) — the viewport *count* is generic; only the renderer
  behind it is not

**The hardcoded-2 choke points (Phase A must touch all of these):**

| Area | Where the 2 lives |
|---|---|
| Join caps | `players->size() >= 2` at glgame.cpp:1166 (add_player2), :1192 (add_remote_player), :5778 (replay ghost join), :8736/:8771 (pad join), :9340 (Enter join) |
| Prefs | `p1_keys`/`p2_keys` named members (preferences.h:118); `binding_for()`'s two-prefix test (preferences.cpp:145); hand-unrolled scalar load/save pairs (preferences.cpp:217–235, :310–342) |
| Key selection | `set_player_keys`: `player_index == 0 ? p1_keys : p2_keys` (glgame.cpp:108) — save-load gives every player past #1 the P2 keymap (glgame.cpp:635) |
| Controller registry | `active_controllers[2]` in state_manager.h:43, loops `for(i<2)` at state_manager.cpp:79/93/120; `controllers[2]` + `opened < 2` in glut.cpp:56/605; same in xbox_main.cpp:90 |
| Renderer | `draw()` calls `draw_world(front(), true)` + `draw_world(back(), false)` only (glgame.cpp:7992–8022); `setup_viewport(bool primary)` with the portrait `//HACK` flip (glgame.cpp:8275) |
| Viewport math | aspect computed as `window.x() / (window.y()/ny)` — missing `/nx` — at glgame.cpp:8052, 8066, 8262, 8336; `camera_screen_radius` takes only `y_viewports` (glgame.cpp:8160) |
| Minimap/divider | `num_y_viewports() == 2 ? y/6 : y/4` (glgame.cpp:8542); single centre divider + minimap hardcoded at `window/2` (glgame.cpp:8544–8589) |
| HUD | `title_text` gated on `size() < 2`, reads `front()` as p1 (view/overlay.cpp:673); `debug_info` drawn only in `front()`'s viewport (view/overlay.cpp:899) |
| Gameplay stragglers | Nova/blast friendly-fire partner check assumes partner is `front()` (glgame.cpp:1825, :4979); Intro dismissal ORs exactly the p1/p2 shoot bindings (intro.cpp:249) |
| Options menu | `OPT_ROWS_DESKTOP` P1/P2 rows (menu.cpp:73); `[2]` state arrays (menu.h:158); hand-unrolled seed/commit (menu.cpp:286, :1930) |
| Save read cap | `read_count(f, cnt, 2)` for players (savegame.cpp:479); `net_state_sane` rejects >2 (net_session.cpp:250) |
| Netplay (Phase B) | one `NetSession`/transport/assembler/delta-baseline; `player_id() = role==Host ? 1 : 2` (net_session.h:198); WELCOME hardcodes id 2 (net_session.cpp:429/524); positional player matching in snapshots (glgame.cpp:5800); signal worker rooms are host+1 joiner (signal/src/worker.js:14/566); lobby strings "PLAYER 2 …" |

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
updated to match. (`edge_indicators` at view/overlay.cpp:329 already does it
right and is the reference.)

**D6 — One join function.** The Enter-join at glgame.cpp:9340 is a hand-copy
of `add_player2` that drops `set_black_holes` (a live inconsistency today).
Phase A extracts `add_local_player(SDL_GameController *ctrl, bool with_keys)`:
allocates the `GLCar`, wires missiles/shock/black-holes/friendly-fire, keys by
the new player's index, pushes, `update_presence()`. All three join paths call
it; the black-holes omission is fixed as a side effect.

**D7 — Per-player visual identity.** P2–P4 are all `GLCar` today. Minimum
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

---

## 3. Phase A work plan (local 4-player)

Ordered so each step compiles and is testable on its own. Estimated shape:
~6 PRs.

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
  join (:9340 — keeps `set_player_keys(…, 1)` and the P2-only semantics), and
  the pad-join sites (:8736, :8771) to it. All caps become `MAX_PLAYERS`.
- state_manager.h/.cpp: `active_controllers[2]` → `[MAX_PLAYERS]`; the three
  `for(i < 2)` loops follow. Same in glut.cpp (:56, :605) and xbox_main.cpp
  (:90). (Do **not** name any new local `near`/`far` — windows.yml is the only
  CI that catches it.)
- overlay join hints (view/overlay.cpp:673–690): the gate `size() < 2` becomes
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
- `draw()` (glgame.cpp:7992): replace the front/back branches with
  `for (i, gs in *players) draw_world(gs, i)` on the offline and NetReplay
  paths (replay ghost cap at :5778 raised to MAX_PLAYERS at the same time —
  4P replays then draw for free). Online branch untouched.
- `setup_viewport(int i)` from `viewport_rect`; delete the `//HACK` flip.
- FOV: keep `view_angle() * 0.75f` whenever the viewport's height is halved
  (ny == 2), which now includes the grid.
- Aspect/audio fixes per D5: glgame.cpp:8052, :8066, :8262, :8336 divide by
  nx; `camera_screen_radius(fov, window, x_viewports, y_viewports)`; update
  callers (:8240 and `net_listener_volume`, which passes 1,1) and the
  CLAUDE.md audio paragraph.
- Minimap/dividers (`draw_map`, :8535): draw the divider cross from
  `viewport_rect` edges; minimap placement per D4 (free cell at 3P, centre at
  4P); replace `num_y_viewports() == 2 ? y/6 : y/4` with a rule keyed on
  "am I in a split at all" plus available cell size.
- Overlay: `debug_info` gate (overlay.cpp:899) becomes "viewport index 0";
  everything else already divides by nx/ny and comes along for the ride.
- WarpPass reads the live viewport rect (glgame.cpp:8495) so it is
  grid-correct for free, but up to 4 captures/frame when lenses are on
  multiple screens — measure on the headless driver; if it hurts, gate to
  N captures/frame round-robin (`lens_on_screen` already skips off-screen).

### A4 — Gameplay N-safety sweep
- intro.cpp:249: dismissal iterates every player's shoot binding instead of
  the p1/p2 pair.
- Nova/blast partner checks (glgame.cpp:1825, :4979): loop all other players
  instead of assuming the partner is `front()`.
- Verify the friendly-fire body-collision site (glgame.cpp:7181) iterates all
  player *pairs* (it sits inside the object-pair loop, so it should — confirm
  with a 3P test, don't assume).
- Revive: mechanics are already N-safe (revives the first fallen; multiple
  fallen partners take successive pickups — one in the world at a time is the
  existing rule and stays).
- `all_players_out()`'s six inlined copies (glgame.cpp:4532, :7706, :8720,
  :8752, :8787, :8925, :9377) are already loops — leave them, or fold into the
  helper opportunistically while touching those files.
- Per-index tint (D7) in GLCar draw + lives/score HUD + minimap dots.

### A5 — Save/replay cap
- savegame.cpp:479: `read_count(f, cnt, MAX_PLAYERS)`; comment the downgrade
  behaviour in savegame.h (old builds ignore 3+-player saves).
- Auto-save/death-save logic (glgame.cpp:7750) is count-agnostic — verify with
  a 3P save/resume cycle that P3 restores with keys, tint, and controller
  re-offer (the pad re-scan at :652 already loops).
- Replays: `Recorder` already takes `player_count`; confirm nothing between
  record and playback truncates indices > 1 (the effect records carry a u8
  player index). `best_path_for` keeps slotting ≥2 as co-op — see Open
  Questions for the leaderboard implication.

### A6 — Tests, shots, docs
- Headless e2e: xdotool can synthesise Enter (P2 join) but not controllers,
  so add a test hook env `NEWTONIA_START_PLAYERS=N` (alongside
  `NEWTONIA_START_GENERATION`/`NEWTONIA_ALL_WEAPONS`) that spawns N players at
  game start, keyboard-inert beyond P2. E2e cases: 4P spawn + split renders
  (screenshot), 3P minimap cell, revive with 2 fallen, 4P save/resume, 4P
  game-over latch, 4P replay playback.
- shots/: a 4P split scene for store assets and layout review (also the
  quickest way to iterate the A3 geometry).
- Update CLAUDE.md (players list semantics, audio note per D5) and TESTING.md.

---

## 4. Phase B outline (online 4-player) — not scheduled

Phase A deliberately leaves online at 2. What B entails, so nothing in A
forecloses it:

1. **Topology**: stay host-authoritative, star of up to 3 clients (mesh is a
   non-starter — the sim is host-only `rand()`). Host holds
   `vector<NetSession*>`; every `net_session_->…` site becomes a fan-out, and
   the per-peer state currently living as scalars on GLGame (input baselines,
   RTT ring, dead-man timers, and critically the `net_known_` delta baseline
   map, glgame.h:634) moves into a per-peer struct.
2. **Identity on the wire**: WELCOME assigns player id 2..4 (net_session.cpp:429
   sends a literal 2 today; the client rejects ≠2 at :524). The protocol
   header already carries a `player_id` byte that *nothing reads*
   (net_protocol.h:340) — routing keys off it instead of attributing every
   message to `players->back()` (glgame.cpp:1649). Snapshot ship records gain
   a player id (the way enemies got `net_ship_id` in PROTO 16) replacing
   positional matching (`is_local = (*it == players->back())`,
   glgame.cpp:5800). `EV_SHIP_IMPACT`'s 1=host/2=client arg widens.
   `Ship`'s static claim outboxes (ship.h:293 …) must become per-ship or carry
   the owner id. All of this is one PROTO bump (flag day, as 21/23 were) plus
   raising `net_state_sane` to MAX_PLAYERS.
3. **Signalling**: the worker's room becomes host + N joiners — joiner
   WebSocket collection, per-joiner offer/answer relay and ICE buffers,
   identity map instead of the `host_identity`/`joiner_identity` scalars,
   `room-full` at 4 (signal/src/worker.js). LAN door likewise multi-joiner.
   Beta worker + protocol tests first, per the deploy-signal pipeline.
4. **Lobby**: player roster UI ("WAITING FOR PLAYERS 2/4"), per-peer
   attestation badges, the "PLAYER 2 …" strings generalised.
5. **Spectate/resume**: spectate cycles living peers; host reclaim and client
   rejoin need per-peer grace bookkeeping — scope carefully, this is where the
   2P resume machinery was hardest.

Rough cost: 3–5× Phase A. Recommend milestoning like NETPLAY.md (M1 host
fan-out over LAN with 2 clients → M2 signalling multi-join → M3
spectate/resume → M4 polish).

## 5. Open questions

- **Leaderboard slotting**: `best_path_for` treats every ≥2 count as one
  co-op slot, so 4P runs would compete on the 2P co-op board. The replay
  header already records the exact `player_count`, so the board *can*
  distinguish — decide with LEADERBOARD.md whether 3P/4P get their own boards,
  share "co-op", or are excluded initially. (Board-side change deploys
  independently of the client.)
- **Achievements**: `coop_clear` currently means "≥2 players" — presumably
  unchanged (4P clears still count). Confirm no achievement text says "two".
- **Options layout**: flat 15-row list vs per-player sub-rows (D9 fallback) —
  decide after seeing the shot.
- **Performance budget**: 4× world draws + up to 4 WarpPass captures on
  low-end desktop — measure before optimising.
