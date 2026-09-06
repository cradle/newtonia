# Steam Input API — plan

Status: **field-verified on Linux with an Xbox Series X pad, portal
registration pending** (2026-09-06; plan written 2026-09-05). Through the
Steam library entry with a layout that binds the game's actions: the
backend adopts the pad, every hint names the layout's position, the FIRE
chip and the F1 card follow a remap, pause/menu/roster/lobby navigate on
the Menu set. What the day's field runs corrected is in §10. Follows the pad-glyph work on
`claude/steam-input-api-support-hk2jll`, which established the rendering
layer (`pad_style.h`) and the one hard fact this plan is built on: the
legacy Steam Input calls do not see the player's bindings. Valve's own
recommendation is the same — use the Steam Input API for broad controller
support. This is the plan for doing that properly; §10 records what
landed against it.

## 10. What landed (2026-09-06) and what is still open

**Code (M1–M3, plus the M4 docs):**

- `pad.h/cpp` — the seam of §1: `PadId`, `pad_attached/pad_name/pad_count/
  pad_id_at/pad_number/pad_axis`, the style cache (folded in from
  `pad_style.cpp`, which is gone), `pad_action_label` (§4) and the
  `ShowBindingPanel` hooks. Every consumer of §1's list holds a `PadId`:
  `GLShip::set_controller(PadId)`, `GLGame`'s constructors, hot-plug and
  roster, `State::nav_key_from_controller(e, PadId*)`, `StateManager`,
  `Intro`, `Menu::confirm_selection(PadId)`, `NetLobby`, `Overlay`,
  `glut.cpp`, `xbox_main.cpp`, `shot_scene.cpp`. No SDL type in `pad.h`
  beyond the button/axis enums the consumers already switch on.
- `steam_input.h/cpp` — the backend of §3: `Init(true)`, `RunFrame` from
  `glut.cpp`'s `tick`, hot-plug by diffing
  `GetConnectedControllers` per tick (chosen over `EnableDeviceCallbacks`:
  one mechanism for startup and later, nothing to keep in step with the
  pump, and §9's flaky device callbacks don't apply), digital edges and
  the stick → synthesized SDL events with ids from `PAD_STEAM_BASE`,
  set switching from `State::pad_action_set()` with a release of the
  outgoing set's held actions, total fallback (also `NEWTONIA_STEAM_INPUT=0`
  as a dev kill switch, and — field, 2026-09-06 — action sets Steam does
  not know: a 0 `GetActionSetHandle` means the In-Game Actions file is
  registered nowhere, so `Init` is undone and SDL drives the pads exactly
  as the shipped build's), every decision under `NEWTONIA_TRACE`
  (`startup_trace.h`, shared with `glut.cpp` now). **§5 rule 1 landed per
  DEVICE, not per backend** (first field run, 2026-09-06: with SDL's
  controller subsystem silenced, "Steam Input off" left no controller at
  all — Steam does NOT present such a pad through the API, contrary to the
  §5 assumption). SDL stays up beside the backend; `glut.cpp` and
  `pad.cpp` skip Steam's own virtual gamepad (vendor 0x28DE, product
  0x11FF — `pad_sdl_device_is_steam_virtual`), which is its emulation of
  the pads the API already presents, so a Steam pad arrives once and a pad
  Steam does not present still arrives through SDL. `SetInputActionManifestFilePath`
  was tried and dropped: it takes an "input manifest" (a file listing the
  IGA and per-type configuration files), not the IGA, and REFUSED the IGA
  (trace, 2026-09-06). Registration is the portal's job (or
  `controller_config/` in development); the depot copy exists for the
  portal's bundled-config path.
- `steam/game_actions_4536720.vdf` — §2's manifest, staged by
  `deploy-steam.yml` into all three depots (macOS also inside
  `Contents/Resources`, ahead of signing) and by `make steam`.
  `test/unit/pad_actions_test.cpp` pins it to `pad.h`'s table. Three
  deviations from the §2 tables, all in the Menu set and all so that TODAY's
  semantics survive the synthesis (the consumers tell B from Back and Start
  from A): `start` (Start: attract dismiss, pause toggle, resume) and `exit`
  (Back: exit-to-menu from the pause screen) are their own actions rather
  than aliases of `confirm`/`back`, and `delete` is not separate from `back`
  — B is both, contextually, in the lobby, and synthesizing the same SDL B
  for both could not have told them apart anyway. `claim` is not an action:
  press-to-claim reads any Menu-set edge, as planned.
- §4: hint sites name ACTIONS (`GLShip::pad_hint(PadAction)`,
  `pad_action_label_any`), the origin → position table covers the seven
  families (Steam Controller, PS4, PS5, Xbox 360/One, Switch, Deck) for
  face/bumpers/triggers/stick clicks and moves/d-pad/Start-Back, anything
  else renders `GetStringForActionOrigin`; triggers and stick moves became
  labelled pseudo-positions in `pad_style.h` (`RT`/`R2`, `LEFT STICK`).
  CONTROLLER LAYOUT rows on the pause menu and the seat roster, Steam pads
  only. `zoom_in`/`zoom_out` actions, synthesized as SDL PADDLE1/2 — which
  also gives an Elite pad's paddles the zoom on the SDL path.

The IGA file is pure KeyValues — no `//` header comment: Steam resolved
the set handles from the commented version, but the layout editor
offered no action sets (2026-09-06), and a comment-free file removes one
variable while that is chased. What the file is and how it is registered
is documented here and in the platform-builds skill instead.

**Field-verified 2026-09-06 (Linux snap client, Xbox Series X pad, the
sniper build through the library entry):** adoption on a layout that
binds the actions, hints following the layout, play + pause + menus on
the two sets, the raw-pad de-dup, Steam Input off (SDL path), a template
remap (SDL path through Steam's emulated pad). Three more rules came out
of it, all in `steam_input.cpp`: a digital action fires only after being
SEEN RELEASED since its set was activated (Start is `pause` in Ship and
`start` in Menu, and a pause switches the set — a held Start toggled the
pause for as long as it was held); hints show the type's default position
until the pad's bindings have loaded (Steam applies a layout on window
focus, and reports every action in-set-but-unbound until then, which read
as "PRESS -"); the F1 card lists only the rows the layout binds and never
circles the "-" marker. And two things learned about the client: the
layout editor on this client adds a duplicate activator per mouse click
(harmless — Steam ORs them) and snaps back to the auto-generated
"Official Layout" on launch in dev mode, so edit that one in place; and a
NON-STEAM SHORTCUT to `steam_run_local.sh` is not the app — Steam's
controller layer keys off the shortcut's id and has no actions for it,
while the API side answers as the real app through `steam_appid.txt`, so
everything looks half-registered. Launch Options on the real library
entry, always.

**Open — needs the Steam client and a pad (the M2/M3/M4 field matrix):**

1. **Portal:** the Steam Input page takes each default layout as a file
   path RELATIVE TO THE INSTALL DIR (the Workshop route is deprecated,
   2026-09-06). So: export the layout from the client (a saved template
   lands under `userdata/<account>/ugc/referenced/<id>/…_controller_config.vdf`),
   copy it into the repo as `steam/controller_<type>.vdf` (`controller_xboxone.vdf`,
   `controller_ps4.vdf`, `controller_ps5.vdf`, `controller_neptune.vdf` for
   the Deck, `controller_switch_pro.vdf`, `controller_steamcontroller_gordon.vdf`
   — Valve's own type names), and the deploy workflow + `make steam` stage
   every `steam/controller_*.vdf` at the depot root beside the actions
   file, so the portal path is the bare file name. The actions file's
   portal path is `game_actions_4536720.vdf`. **Until a default layout
   exists for a pad's type, that pad has no bindings under Steam Input** —
   the backend hands it to SDL, so the game still plays, but without the
   layout-aware hints — so this step gates the first beta push of this
   branch, not just the polish.
2. Field matrix from §7: Xbox + DS4 on the sniper build through the
   library entry; pause/roster/lobby nav and code entry; hot-plug and
   2 pads; a remap of A/B in the overlay followed by the F1 card and the
   FIRE chip within a frame; a grip binding showing Steam's text; the
   Deck (trackpad-as-stick feel, §8); macOS + Windows clients; the
   Init-false and Steam-Input-disabled-per-pad paths.
3. Two known seams to watch in that matrix: (a) with `fire` collapsing
   RT into A, a FRESH trigger pull on the disconnect card now confirms it
   (a held one still does not — edges only); (b) `SteamInputConfigurationLoaded_t`
   is not consumed — a pad whose layout arrives late, or sits on a gamepad
   template, reads `bActive == false` and is left to SDL until a layout
   with the actions loads (then adopted within a tick).

**Mixed mode (Steam Input + SDL pads at once) — landed 2026-09-06**, the
same day it was filed as future work, because the first field runs showed
the alternative does not exist. What the traces taught, in order:

- With no official layout, Steam runs an Xbox pad on its **legacy gamepad
  template**: the API still presents the handle, but every action reads
  `bActive == false` (not in the configuration), and Steam emulates an
  XInput pad that SDL sees as `Xbox Series X Controller` with the REAL
  vendor/product (045e/0b12 — the 0x28DE/0x11FF "Steam Virtual Gamepad"
  ids are a Windows thing). So the pad "worked" through SDL while every
  hint read "-" from the bound-nothing Steam pad.
- "Steam Input disabled" on the snap client still presented the handle
  (no actions) AND still grabbed the physical device, so SDL's copy was
  silent — the pad had no working path. Whether the shipped build behaves
  the same there is the control run still owed.

The rule as landed — **adoption by actions, ownership per device**:

- `steam_input.cpp` tracks every connected handle but ADOPTS one (assigns
  a `PadId`, announces `controller_added`, polls it) only while its
  layout uses the game's actions — `any_action_active` on the wanted set
  each tick. A handle on a gamepad template is left to SDL, whose
  emulated device is the pad exactly as on the shipped build; an adopted
  pad whose actions all go inactive for `INACTIVE_DROP_TICKS` is
  released back (`controller_removed`). The switch is live, so picking a
  layout in the overlay moves the pad between backends without a restart.
- The SDL side de-dups by HANDLE, not vendor id: SDL ≥ 2.30's
  `SDL_GameControllerGetSteamHandle` names the handle behind a Steam
  virtual gamepad; `glut.cpp` probes each device once on first sight
  (`sdl_probe_steam_handle` → `pad_sdl_note_steam_handle`) and
  `pad_sdl_device_is_steam_virtual` checks that handle LIVE against the
  backend's adoption (`steam_input_owns_handle`). A ~250 ms
  `sdl_pads_sync` closes an opened SDL pad the backend now drives and
  opens an unopened one it no longer does. Enumeration (`pad_count`/
  `pad_id_at`) unions adopted Steam pads first, then the listed SDL pads.

- Third run, same day: SDL DID enumerate the physical pad beside Steam's
  virtual one — instance 0 (045e/0b12, no handle) from the startup scan,
  then Steam's virtual gamepad recreated three times as it loaded configs
  (instances 1→3, each carrying the handle) — and both delivered the same
  input, so two seats played as one. Not a silent phantom: SDL's hidapi
  path reads the raw pad regardless of Steam's evdev grab. The rule now
  covers it: while Steam presents handles and every presented handle
  already has a driver (adopted by the backend, or an SDL device carrying
  that handle), a handle-less SDL device is the physical duplicate and is
  skipped; while some handle has no driver yet it is kept (the pad must
  work even if SDL cannot read the handle), and with no handles presented
  at all (Steam Input off for the game — which now works, display and
  buttons correct) the raw device is the pad. A mixed rig — one pad on
  Steam Input, a second with it disabled — loses the second under this
  rule; Steam's own `SDL_GAMECONTROLLER_IGNORE_DEVICES` mechanism makes
  the same assumption, and the trace now prints whether that env var
  reached the process at all.

Still to field-verify in the §7 matrix: a layout that binds the actions
(the adoption line in the trace, then FIRE/START chips following a
remap); one Steam pad + one SDL pad in two seats; the control run above.

## 0. Why, and what already exists

**Why.** Three things only the Steam Input API gives:

1. **Remap-aware hints.** Steam keeps the player's layout; the game only
   learns it through action origins (`GetDigitalActionOrigins`). With the
   gamepad-emulation path the game sees an Xbox pad and nothing else, and
   `GetActionOriginFromXboxOrigin` translates pad TYPES, not bindings —
   verified with A/B swapped in the app's own layout on the Linux and macOS
   clients (F1 card still read A; CLAUDE.md's pad glyph note has the trace).
2. **Every pad Steam supports, without SDL needing a mapping for it.** Steam
   Controller, Deck's back grips and trackpads as first-class inputs,
   third-party pads Steam knows and SDL doesn't, and Steam's own dead-zone,
   gyro and haptics handling.
3. **Player-facing remapping done for us.** `ShowBindingPanel` opens the
   overlay's configurator for our actions; no in-game rebinding screen for
   pads, ever.

**What exists.** `pad_style.h/cpp` renders any (style, SDL position) pair
as the pad's own glyph — letters, PlayStation shapes, L1/LB, OPTIONS/START
— through Typer, with a unit test. Every hint site already asks it. That is
exactly the rendering layer this plan feeds; it does not change.

**What this is not.** Not a replacement for SDL: the desktop, web, Android,
iOS and Xbox builds keep SDL's GameController path untouched, and so does a
Steam build launched outside Steam or on a client where `Init` fails.

## 1. The central design decision: a pad seam, then a second backend

Today the game talks to SDL directly about pads in ~14 call sites:
`GLShip` holds an `SDL_GameController*` and an `SDL_JoystickID`, `GLGame`
enumerates `SDL_NumJoysticks()` for the join hint and the seat roster,
`State::nav_key_from_controller` keys its per-pad hysteresis on the
instance id, and every consumer (`GLShip`, `GLGame`, `Menu`, `NetLobby`,
`Intro`, `Overlay`, `state.cpp`) switches on `SDL_Event`
`SDL_CONTROLLERBUTTON*` / `SDL_CONTROLLERAXISMOTION`.

Under the Steam Input API a controller is an `InputHandle_t`, there is no
SDL device (Steam does not emulate a gamepad for a pad the API owns), and
there are no events — the game polls action data each frame. Two ways to
bridge that:

- **(A) Synthesize SDL events.** The Steam backend polls actions per tick
  and feeds `StateManager::controller()` synthetic
  `SDL_CONTROLLERBUTTONDOWN/UP` and `SDL_CONTROLLERAXISMOTION` events with
  synthetic instance ids, plus `controller_added/removed` for hot-plug.
  Every consumer stays as it is.
- **(B) A new input abstraction** (actions in, both backends produce them),
  rewriting every consumer.

**Decision: (A), behind a small pad seam.** The consumers' vocabulary —
"button A went down on pad N" — is already an action vocabulary with an
Xbox accent; the manifest below is a one-to-one relabelling of it. (B)
would rewrite eight files to say the same thing. What (A) needs is that the
game stop touching SDL objects directly for pads:

`pad.h` (new): `typedef int PadId;` plus `pad_attached(id)`, `pad_name(id)`,
`pad_style(id)` (moves in from `pad_style.h`), `pad_count()`,
`pad_id_at(index)`, `pad_is_free(id)` — the whole surface the game code
uses today, listed by `grep SDL_GameController glship.cpp glgame.cpp
state.cpp menu.cpp net_lobby.cpp intro.cpp view/overlay.cpp glut.cpp`. The
SDL backend implements it over the existing `controllers[]` table in
`glut.cpp`; `GLShip::controller` becomes a `PadId`. **This refactor is
milestone 1 and changes no behaviour** — it is fully testable with the
existing pads before a line of Steam code exists.

The Steam backend (milestone 2) then owns a table `PadId ↔ InputHandle_t`,
allocates ids from a range SDL never uses (SDL instance ids are small
non-negative ints; use `1 << 20` upward), and is the ONLY source of pad
events on a Steam build where `Init` succeeded — see §5 on why SDL's
controller subsystem must be silenced there, not merely ignored.

## 2. The action manifest

One In-Game Actions file, `steam/game_actions_4536720.vdf`, derived from
the bindings the game hard-codes today (`GLShip::controller_input`,
`controller_axis_input`, `State::nav_key_from_controller`, the lobby's
CodeEntry picker, `Intro`). Two action sets; no layers needed yet.

**`Ship`** (in play, and on the Intro screen, whose only input is fire):

| Action | Kind | Today's Xbox binding | Notes |
|---|---|---|---|
| `steer` | analog `joystick_move` | left stick | X/Y → the existing stick handlers |
| `thrust` / `reverse` / `turn_left` / `turn_right` | digital | d-pad | the d-pad pilots' path |
| `fire` | digital | A, right trigger | both today; ONE action, Steam's default layout binds both |
| `secondary` | digital | B, left trigger (`l2_shoot_active`) | same |
| `next_weapon` / `next_secondary` | digital | X / Y | |
| `boost` | digital | LB | |
| `teleport` | digital | RB | |
| `rotate_view` | digital | L3 | |
| `help` | digital | R3 | the F1 card |
| `pause` | digital | Start | also "press start to join" for an unseated pad |
| `menu` | digital | Back | the online-safe pause/back path |
| `zoom_in` / `zoom_out` | digital | (none today on pads) | new — free win, matches the keyboard rows |

**`Menu`** (main menu, options, stats, pause menu, seat roster, lobby,
replays):

| Action | Kind | Today's binding |
|---|---|---|
| `nav` | analog `joystick_move` | left stick (with the arm/release hysteresis kept in `state.cpp`) |
| `nav_up/down/left/right` | digital | d-pad |
| `confirm` | digital | A, Start, right trigger |
| `back` | digital | B, Back |
| `paste` / `keyboard` | digital | X / Y (lobby code entry) |
| `delete` | digital | B (lobby code entry, distinct from `back` so a layout can separate them) |
| `claim` | digital | any button — modelled as `confirm` + the roster's press-to-claim reading ANY `Menu` action edge |

Localization block for every action title and set name (the configurator
shows them). Default bindings per controller type are authored ONCE in the
configurator from a Deck or the desktop client and exported; the file
ships in the depot root on all three platforms (`deploy-steam.yml` stages
it beside the binary) and is registered in the portal as **Custom
Configuration (Bundled with Game)** with that relative path. Development:
copy it to `<Steam>/controller_config/game_actions_4536720.vdf`.

**Set switching** is driven by the existing state machine: `StateManager`
knows which `State` is current; the Steam backend calls
`ActivateActionSet(handle, Ship|Menu)` on every pad when the active state
changes, and `GLGame` flips to `Menu` while its pause menu, seat roster or
help card is up and back to `Ship` on resume. (Per-pad, so a P2 sitting in
the pause roster while P1 plays is not a case — the pause is global.)

## 3. Reading input — the synthesized-event contract

Per tick (`glut.cpp`'s `tick`, before `game->tick`), on a Steam build with
`Init` succeeded:

1. `RunFrame()` (explicit — `Init(true)`, so polling cadence is ours, not
   the callback pump's).
2. For each connected handle: read every digital action in the active set;
   an edge → one synthetic `SDL_CONTROLLERBUTTONDOWN/UP` with the button
   the action's TODAY binding used (`fire` → `SDL_CONTROLLER_BUTTON_A`,
   `secondary` → B, and so on — the table in §2 read right-to-left). Read
   `steer`/`nav` → `SDL_CONTROLLERAXISMOTION` LEFTX/LEFTY scaled to
   ±32767, only on change beyond the existing 8000-count thresholds' input
   resolution. Fire's trigger alias collapses into the button: the consumers
   already treat A and RT identically.
3. Device callbacks (`EnableDeviceCallbacks`, `SteamInputDeviceConnected_t`
   / `Disconnected_t`) → `StateManager::controller_added/removed` with the
   synthetic id, so `GLGame::controller_added`'s reconnect logic
   (`awaiting_pad`, FOURPLAYER.md) runs unchanged.

Nothing downstream learns that the source changed. That is the whole point
of (A): `GLShip`, the nav ladder, the lobby picker, the roster's
press-to-claim and the "player N press start to join" hint all keep working
on the same event shapes they consume today, and the FOURPLAYER.md
behaviours (one input drives one ship, hot-plug reconnect, press-to-claim)
carry over by construction.

## 4. Glyphs that follow the layout

`pad_style.h` stays the renderer. The Steam backend adds one lookup:
`pad_action_label(PadId, action)` → for a Steam pad,
`GetDigitalActionOrigins(handle, set, action)` gives the physical origin(s)
in the CURRENT layout; map the origin onto `(style, SDL position)` with a
table covering the families Steam reports for the pads we care about
(`XBox360_*`, `XBoxOne_*`, `PS4_*`, `PS5_*`, `SteamDeck_*`, `Switch_*` —
face buttons, bumpers, triggers, sticks, d-pad, Start/Back equivalents),
and render through `pad_button_label`. Anything outside the table (a back
grip, a trackpad click, the Deck's rear buttons) renders
`GetStringForActionOrigin` as plain text — the weapon-row chips already
handle a word label. For an SDL pad the lookup is the type table, as now.

Hint sites switch from "the button" to "the action": `SHOOT` asks for
`fire`, the pause hint for `pause`, the lobby key line for `paste`
/`keyboard`/`delete`, the join hint for `pause`. Eight sites; the same ones
the glyph branch touched.

Not doing: Steam's PNG/SVG glyphs (`GetGlyphPNGForActionOrigin`). The game
is vector-drawn through Typer and the vocabulary is already there; a bitmap
would be the one non-vector element on screen.

`ShowBindingPanel(handle)` gets a row on the pause menu and the seat roster
("CONTROLLER LAYOUT"), shown only for Steam pads.

## 5. The two rules that make it safe

1. **A pad has exactly one owner.** On a Steam build where `Init` succeeded,
   Steam owns every controller it reports, and the SDL controller subsystem
   must not also open them — otherwise a pad Steam still emulates (a
   layout with legacy gamepad output, a pad Steam Input is disabled for)
   arrives twice. ~~Rule: after `Init`, SDL is initialised WITHOUT
   `SDL_INIT_GAMECONTROLLER`, and the SDL pad backend reports zero pads.
   A pad the player has disabled Steam Input for is then Steam's problem to
   present (it does: such pads still appear through the API with a default
   layout).~~ **Falsified in the first field run (2026-09-06):** a pad with
   Steam Input disabled is NOT presented through the API, so this version
   of the rule left it with no controller at all. The rule as landed is
   per DEVICE: SDL stays up, and the SDL side skips Steam's virtual
   gamepad (the emulation of a pad the API presents), so each physical pad
   arrives exactly once — see §10.
2. **Fallback is total, never partial.** `Init` false (not launched by
   Steam, client too old, Steam absent) → the SDL backend exactly as today,
   Steam backend never consulted, `steam_appid.txt` terminal runs keep
   working. Decided once at startup; no runtime switching.

## 6. Netplay, replays, saves — untouched

Input is applied locally on every machine: a client's presses reach the
host as `MSG_INPUT` after the seat's `Ship` has consumed them, so the
backend behind the events is invisible to the protocol. Replays record
state, not inputs. Preferences keep only keyboard bindings; pad layouts
live in Steam. No PROTO, savegame or INI change.

## 7. Milestones

| # | Work | Verifies as | Size |
|---|---|---|---|
| M1 | `pad.h` seam over SDL; `GLShip` holds a `PadId`; the 14 SDL call sites move behind it; `pad_style.h`'s runtime half folds in. **No Steam code.** | Every existing pad behaviour on desktop + the shipped Steam build: seat roster, press-to-claim, hot-plug reconnect (FOURPLAYER.md), 4 pads. e2e suite green. | ~1 day |
| M2 | Manifest + Steam backend: `Init(true)`, handle table, `RunFrame`, action reads → synthetic events, device callbacks, set switching from the state machine, the SDL-silencing rule. Portal: upload manifest, author default layouts (Xbox, PlayStation, Deck, Switch, Steam Controller), bundled-config setting. `steam_stub` grows the API surface used. | Sniper build via `steam_run_local.sh` (platform-builds skill): play a level on Xbox + DS4, pause/roster/lobby navigation, code entry, hot-plug, 2 pads, remap A/B in the overlay and play with it. Beta depot on Deck. | ~2 days |
| M3 | Origins → glyphs (`pad_action_label`, the origin table, text fallback), hint sites on actions, `ShowBindingPanel` rows, `zoom_in/out` actions. | Remap A/B → F1 card and FIRE chip follow within a frame; a grip/trackpad binding shows Steam's text; DS4 shapes. | ~1 day |
| M4 | Fallback + edge cases: `Init` false path, Steam Input disabled per pad, pad disconnect mid-game under the API, Deck sleep/resume, macOS + Windows clients. Docs: CLAUDE.md pad note, FOURPLAYER.md cross-refs, TESTING.md matrix, `platform-builds` skill. | The M2 matrix again on all three desktop platforms + Deck; the shipped-build regression list from M1 once more. | ~1 day |

M1 is worth landing on its own even if the rest waits: it removes the
last direct SDL dependency from game logic and is the precondition for any
other pad backend (the Xbox fork's GDK input is the other obvious one).

## 8. Open questions (decide before M2)

- **Trigger-as-fire.** Today RT and A both fire; under the API `fire` is
  one action and Steam's default layout binds both physical controls to
  it. Keep it that way (no `fire_alt` action) — a layout can split them.
- **`Menu` while a co-op partner plays.** Pause is global, so per-pad set
  switching is a non-issue today; if a per-seat overlay ever appears
  (roster for one seat), sets become per pad — the API supports it.
- **Deck trackpad tap-to-fire** (`controller_touchpad_input`, the Deck's
  left pad as a joystick) — under the API this becomes a `Ship` layout
  choice (trackpad → `steer`) and the SDL touchpad handler retires on Steam
  builds. Confirm on a Deck that the default layout's trackpad feel matches.
- **Gyro / haptics** — out of scope; the manifest can grow a `gyro` analog
  action later without touching the seam.
- **Xbox fork.** Its GDK input path is a third backend behind the same
  seam; not this plan's job, but M1's design should not preclude it
  (`PadId` stays an opaque int; no SDL types in `pad.h`).

## 9. What tonight's dead ends teach the implementer

- Test Steam-launched behaviour ONLY through the library entry
  (`steam_run_local.sh`), never a non-Steam shortcut, and only with a
  runtime-built binary (`build_steam_sniper.sh`, glibc floor ≤ 2.31) — the
  platform-builds skill has the details and the traps.
- `NEWTONIA_TRACE=/path` is the only reliable way to see startup output
  under Steam's runtime container; extend it with the backend's decisions
  (`Init` result, handles, set switches) from the first commit.
- A second login of the same account elsewhere logs this client out
  mid-test and reads like a crash.
- Hot-plug is flaky on the snap client even on the shipped build; keep a
  default-branch control run in the matrix so a regression is never
  mistaken for one.
