# Steam Deck default layout — findings (2026-09-07)

The one open item from the Steam Input API work (STEAMINPUT.md): on a Steam
Deck the game's official layout is offered but not applied by default. This
file records what was established, what was ruled out, and why it could not
be closed from the game's side.

## What works

- The Steam Input backend (`steam_input.cpp`) on the Deck: sets resolve at
  Init (`Init ok, sets Ship=1 Menu=2`), the Deck's handle is presented, and
  once "Newtonia Official (Steam Deck)" is picked the pad is adopted, drives
  seat 1, and every hint follows the layout. Switching layouts in either
  direction mid-game hands the pad over cleanly (the SDL-twin-first fix,
  `sdl_pads_sync_now`).
- `steam/controller_neptune.vdf` is a genuine Deck export: authored on the
  device in the official layout's editor, `controller_caps` 23117823 (the
  Deck's own value), every Ship and Menu action bound, both analog actions.
- The same manifest applies its PlayStation layout to a DualSense on the
  desktop client by default, from the beta depot, with no hand-picking.
- The Deck's layout picker lists ours under Templates as **"Recommended
  Template — Newtonia Official (Steam Deck)"** with the Steam Input API badge,
  so the client has parsed the manifest, found the neptune configuration,
  and recognises it as the developer's recommended layout.

## What does not

A first launch on the Deck lands on Valve's **"Gamepad With Joystick
Trackpad"** template. The game then sees a handle with no active actions
(SDL drives it through Steam's emulated device, exactly like the shipped
build), so the layout-aware hints and the adopted-pad path are unused until
the player opens the picker.

## Ruled out, with the evidence

| Hypothesis | Test | Result |
|---|---|---|
| Manifest shape | `configurations` rewritten to Valve's documented shape (keyed by controller type, then priority, each entry a `path`) | Desktop client started applying the PlayStation layout; Deck unchanged |
| Seeded file (Xbox export retyped) | Replaced by a real Deck export; then the header stripped of the personal `workshop://` URL and given a real timestamp | Listed as Recommended Template; still not applied |
| Stored per-app choice | `configset_<serial>.vdf` inspected: empty. "Revert to shared configuration" used | Unchanged |
| Steam Cloud restoring an old shared configuration | `Steam Controller Configs/<user>/config/4536720-beta/` (a July 26 generic-template autosave inside) deleted with Wi-Fi off, client restarted offline | Still the generic template with nothing on disk |
| Client channel | PC and Deck both on the Steam client beta | Same channel, different choice |
| Steam Input Layout Dev Mode | Layout offered with it on and off | No effect on the default |
| Portal opt-in ticks | Xbox and PlayStation ticked and published; "Generic (DirectInput)" and "Any Future Devices" tried as well | Awaiting the last result at time of writing; expected no effect, the Deck's built-in controller is not in that list |
| Trace-side | Game logs identical on every Deck launch: manifest set, sets resolved, handle presented, no actions active | The game is not the deciding party |

## Why it could not be solved here

The decision is made inside the Steam client before the game starts, and
Valve documents the setup (Custom Configuration + manifest path + per-type
opt-in) but not the selection rule. The portal's own text ties the Custom
Configuration to "controllers opted into Steam Input", and the opt-in list
has no entry for the Deck's built-in controller, so the Deck may be
following that wording literally: it recognises the developer layout as
*recommended* but keeps Valve's template as the *default* for its own
hardware. Nothing the game does at runtime can change that choice, and no
file we ship has been shown to influence it once it is a genuine Deck export.

## What ships in the meantime

- The CONTROLLER LAYOUT row on the pause menu and the seat roster now
  appears for ANY pad Steam presents, adopted or not
  (`pad_has_binding_panel_any`), and opens Steam's layout page for it. A
  Deck player on the generic template reaches the picker from inside the
  game and picks "Newtonia Official (Steam Deck)" once; Steam remembers it.
- Diagnostics for the next round: `SteamInputConfigurationLoaded_t` is
  traced on every layout Steam hands the game (creator, revision, whether
  it uses the Steam Input API), and an un-adopted handle dumps its state
  every ~5 s (`NEWTONIA_TRACE=1 %command%` writes
  `$HOME/newtonia-trace.txt`).

## How to close it

1. **Steamworks support ticket** (the only route to the rule itself):
   app 4536720, Steam Input API, action manifest published as the Custom
   Configuration with `controller_xboxone`, `controller_ps5` and
   `controller_neptune` configurations, each a client export on that type;
   desktop applies the PlayStation layout by default, the Deck applies
   "Gamepad With Joystick Trackpad" with ours shown as the Recommended
   Template; reproduced with all stored configurations deleted, offline, on
   the client beta. Attach the three layout files, the manifest and a trace.
2. **Steam Deck compatibility review**: Valve's reviewers can pin a
   recommended layout for the Deck, and the review is due anyway before the
   store lists the game as Verified.
3. **A fresh account on a Deck** (Family Sharing) for a true first-launch,
   to rule out anything account-scoped that the offline test could not.
