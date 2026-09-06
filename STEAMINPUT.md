# Steam Input API — plan

Status: **proposed, not started** (2026-09-05). Follows the pad-glyph work on
`claude/steam-input-api-support-hk2jll`, which established the rendering
layer (`pad_style.h`) and the one hard fact this plan is built on: the
legacy Steam Input calls do not see the player's bindings. Valve's own
recommendation is the same — use the Steam Input API for broad controller
support. This is the plan for doing that properly.

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
   arrives twice. Rule: after `Init`, SDL is initialised WITHOUT
   `SDL_INIT_GAMECONTROLLER`, and the SDL pad backend reports zero pads.
   A pad the player has disabled Steam Input for is then Steam's problem to
   present (it does: such pads still appear through the API with a default
   layout). This is the one behavioural cliff of the whole plan and gets
   its own field test.
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
