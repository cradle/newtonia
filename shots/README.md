# Screenshot harness

Renders one composed scene — the menu, or a game world built to order — at
an arbitrary window size, writes a PNG, and exits. For store/marketing
shots, reference cards, and visual bug reports, all reproducible from the
command line. Runs on any desktop build (netplay or `NETPLAY=0`), windowed
or headless under Xvfb.

## Quick start

```sh
make NETPLAY=0                  # any desktop build works
shots/run.sh                    # render every shots/*.shot -> shots/out/
shots/run.sh shots/hero.shot    # just one
NEWTONIA_SHOT_SIZE=2560x1440 shots/run.sh shots/hero.shot
```

Windows (PowerShell, after the MSYS2 build — see CLAUDE.md): no Xvfb
involved, a real window opens briefly per scene, rendered on the actual
GPU. If the requested size is taller than the desktop, the window manager
may clamp the window — the `shot: wrote` line prints the size actually
captured, so check it. On a 1080p display set
`NEWTONIA_SHOT_FULLSCREEN=1` to render fullscreen at exactly the desktop
resolution instead.

```powershell
.\shots\run.ps1                            # every shots\*.shot
.\shots\run.ps1 shots\steam1_level1.shot   # just one
$env:NEWTONIA_SHOT_FULLSCREEN=1; .\shots\run.ps1   # 1080p-display fix
```

**Seeds are per-platform.** The world generator rides the C runtime's
`rand()`, and MinGW's differs from glibc's — the same scene renders the
same *composition* everywhere, but the generation-spawned background
(rock arrangement, shapes, spawn pocket, starfield) rolls differently
per OS. Iterate the scene's `seed` on the machine you render on until
the background looks right; it is stable there from then on.

Or drive the binary directly (this is all `run.sh` does):

```sh
SDL_AUDIODRIVER=dummy \
NEWTONIA_SHOT=out.png NEWTONIA_SHOT_SCENE=shots/hero.shot \
NEWTONIA_SHOT_SIZE=1920x1080 \
xvfb-run -a -s "-screen 0 1920x1080x24" ./newtonia
```

## Environment variables

| Variable | Meaning |
|----------|---------|
| `NEWTONIA_SHOT=out.png` | Enables shot mode; output path (`.png`, or `.bmp` via SDL) |
| `NEWTONIA_SHOT_SIZE=WxH` | Window/image size (beats the scene's `size`; default: preferences) |
| `NEWTONIA_SHOT_SCENE=file` | Scene script (default: a plain new game) |
| `NEWTONIA_SHOT_MS=N` | Simulated ms before capture (beats the scene's `sim`) |

Shot mode never touches real player data: no Steam init, no preference
writes, replay recording disabled, savegame/high-score/stats paths latched
off (the game is marked cheated). The sim runs on a fixed 16 ms step, so a
scene's `seed` makes the output byte-for-byte reproducible.

The existing debug env vars compose: `NEWTONIA_ALL_WEAPONS=1` fills the
HUD's weapon list, `NEWTONIA_TEST_SPAWN_PICKUPS=1` rings every pickup
around the spawn.

## Scene scripts

Line-based; `#` comments. Positions are **world units relative to player
1's spawn**, which is the camera centre: `(0,0)` is mid-screen, +y up. At
the default zoom about ±900 units are visible vertically (the horizontal
span scales with the aspect ratio). Everything is optional — an empty
scene is a plain new game.

```
size 1920 1080         # window size (or "size 1920x1080")
sim 1500               # simulated ms before capture (default 1000)
seed 7                 # RNG seed: same scene, same shot (default 1337)

menu                   # base: the menu instead of a game (attract screen;
                       #   `key enter` to reach the main menu)
game 14                # base: in-game at generation N (worlds, hazards,
                       #   stations and counts the game would have by then)

clear                  # empty the generated world (asteroids, hazards,
                       #   pickups, black holes) before composing
hud off                # hide the HUD and minimap
stars 0.15             # starfield density 0..1, overriding the preference
                       #   (shot mode never writes preferences back; 0 = none)
transparent            # write RGBA: black becomes full transparency, dim
                       #   stroke edges translucent (logo/text assets)
noship                 # hold every player unspawned — pure-scenery shots
                       #   (title cards, capsules); camera stays put
zoom 60                # vertical FOV degrees (default 85; smaller = closer;
                       #   much wider shows the toroidal wrap copies)
camera fixed           # keep the world's orientation on screen. Default is
                       #   the game's default, `rotate`: the view follows
                       #   the ship's heading, so the ship ALWAYS draws
                       #   pointing up and world placements appear rotated
                       #   by -heading. Composed scenes with angled ships
                       #   want `fixed` (WYSIWYG).
players 2              # local split-screen co-op

ship 0 -150 25         # move player 1 (and the camera) / heading in degrees
                       #   (0 = up, positive = counter-clockwise)
ship2 300 0 -30        # move player 2 (with `players 2`)

# TYPE flags combine; r= clamps to Asteroid::max_radius; v= units/second.
asteroid X Y [normal|invincible|invisible|reflective|teleporting|quantum|
              tough|armoured|phasing]... [r=R] [v=VX,VY]
enemy X Y [difficulty]         # AI ship (it will fight!)
hazard pulsar|comet|seeker X Y [v=VX,VY]   # a natural comet cruises ~280 u/s
blackhole X Y
pickup TYPE X Y        # weapon mine giga missile shield god nova beam
                       #   lance shock revive life

text X Y SIZE WORDS... # caption in the Typer font (uppercased). X/Y are
                       #   window fractions, -1..1, (0,0) centre, +y up,
                       #   anchored at the text's CENTRE both ways — so
                       #   `text 0 0` is dead centre. SIZE like the menus
                       #   use (5 small, 16 title, 80+ capsule art)
key NAME [at_ms]       # synthesized key press at sim time (default: 200,
                       #   then +400 per key). NAME: a character, or
                       #   enter space esc up down left right
hold NAME [at_ms]      # like key, but never released — thrust flames and
                       #   autofire stay live in the captured frame.
                       #   P1: w thrust, space shoot. P2: i thrust, / shoot
```

Notes:

- The camera **is** player 1's ship — `ship` moves both. Objects with
  velocities (and homing seekers, cruising comets, AI enemies) move during
  `sim`, so compose where things should be **at capture time**; the fixed
  seed keeps the result stable once it looks right.
- Ships spawn alive with the spawn-shield ring already expired, and
  generation-spawned asteroids are swept clear of every placed ship and
  enemy (composed spawns are left where you put them). Composed enemies
  wake alive with their AI lock delay skipped, so they engage within the
  sim window. At capture the harness logs one `shot: player N alive=...`
  line per player (plus an enemies line) so a script can assert the cast
  survived without eyeballing the render.
- State transitions are deliberately not followed: `key`-ing the menu into
  NEW GAME leaves the menu on screen (the Menu's own screens — options,
  replays — are fine, they're the same state). Use `game` for gameplay.
- Keep `sim` under ~5000 for a `clear`ed scene, or leave a killable
  asteroid in it — with nothing left to destroy the level-clear countdown
  would rebuild the world mid-shot (the harness guards the common case).

## Example scenes

| Scene | Shows |
|-------|-------|
| `hero.shot` | Composed asteroid field + title captions |
| `menu.shot` | Main menu (attract dismissed via `key enter`) |
| `specials.shot` | Labelled reference card of the special asteroid types |
| `hazards.shot` | Pulsar, comet, seeker and a black hole, HUD off |
| `lategame.shot` | Natural generation-14 chaos (station, black hole, full counts) |
| `steam*.shot` | The five 1920x1080 store screenshots (levels 1, 5 co-op, 5, 14, 20) |
| `capsule_*.shot` | Store capsule art: the Typer title over bare starfield (`noship`) at each Steam capsule size — the library card and `capsule_library_header` banded with monochrome asteroid clusters — plus a bare-starfield page background |
| `logo_transparent.shot` | 1280x720 RGBA logo: just the word, transparent everywhere else |
