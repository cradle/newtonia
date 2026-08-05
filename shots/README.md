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
| `NEWTONIA_SHOT_FRAMES=N` | Capture N frames instead of one — clip mode (see below) |
| `NEWTONIA_SHOT_FPS=N` | Clip frame rate (quantised to the 16 ms sim step) |

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
frames 90              # capture a CLIP of N frames, not one still (default 1)
fps 30                 # clip frame rate (default ~31; quantised to the 16 ms
                       #   sim step, so the harness logs what it really used)
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
tap NX NY [at_ms]      # synthesized touch tap (0..1 fractions, top-left
                       #   origin) — the authentic input for touch-layout
                       #   shots (dismisses TAP TO START, hits tap bands)
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

## Clips (GIF + MP4)

`frames N` turns a scene into a clip: the harness captures N frames, advancing
`1000/fps` ms of simulation between each, writing `out_0000.png`,
`out_0001.png`, … beside the still's path. `shots/gif.sh` renders the scenes
in `shots/clips/` and assembles each into a looping MP4 and GIF in
`shots/out/clips/`:

```sh
shots/gif.sh                                # every shots/clips/*.shot
shots/gif.sh shots/clips/lance.shot         # just one
NEWTONIA_GIF_WIDTH=480 shots/gif.sh         # narrower GIF (smaller file)
NEWTONIA_GIF_KEEP=1 shots/gif.sh            # keep the PNG frames
```

These are the social/feed assets — see PROMOTION.md §3. Prefer the MP4
wherever the target takes video; it is half the size and cleaner than the GIF.

Frame intervals are quantised to the loop's fixed 16 ms sim step, so a
requested fps that doesn't divide evenly is rounded (25 → 20). The harness
logs the interval it actually used and `gif.sh` reads that back, so playback
always matches simulated speed. Everything that makes a still reproducible
holds for a clip: same seed, same frames.

| Scene | Shows | Reads well? |
|-------|-------|-------------|
| `lance.shot` | Lance pulse mirror-bouncing off reflective asteroids | **Yes** — the strongest of the set |
| `pulsar.shot` | Pulsar shockwave expanding and shoving the ship | **Yes** |
| `shock.shot` | Chain lightning hopping a ladder of rocks, stopped by a tough one | Yes |
| `gameplay.shot` | Plain generation-5 play, flown and shot | Yes — the "what is it like" clip |
| `blackhole.shot` | Rocks curving in and vanishing | Marginal — see below |
| `invisible.shot` | Invisible asteroids as starfield lens distortion | Marginal — see below |

**Two effects do not survive capture, and it is worth knowing why before
spending an afternoon on them.** The invisible asteroid's lens is a
screen-space warp with no outline of its own: it needs dense stars and a big
on-screen radius even to be noticed, and at GIF resolution it is close to
nothing. The black hole draws *nothing at all* in the world view — its ring is
minimap-only — so a clip can only show it by the curve of what falls in. Both
scenes are kept, correct and documented, but neither is a lead asset.

Gotchas that cost real time here:

- **The camera IS player 1**, so `ship` cannot be used to move the pilot out
  of a hazard's way — it slides the whole view too. Leave the ship at the
  origin and route the hazard past it.
- A big rock that reaches the ship kills it and sheds **visible** fragments,
  which in an invisible-asteroid scene gives the whole trick away.
- Hazards on fixed cycles need the window timed to them: the pulsar charges
  for `PULSAR_CHARGE_MS` (2400) before a `PULSAR_EXPAND_MS` (1100) expansion,
  so an untimed clip catches a blinking star and nothing else.
- **`hud off` for anything public.** In one-player the HUD carries a
  "PLAYER 2 PRESS ENTER TO JOIN" banner, which is not something to put in a
  marketing clip.
- Semi-automatic weapons (beam, lance, shock) fire once per trigger pull, so
  they want repeated `key space` lines — a `hold` fires a single shot.
- Dropping a weapon `pickup` on the spawn both arms and **selects** it
  (`Ship::add_lance_ammo` and friends), which is the tidiest way to put a
  specific weapon in the ship's hands at frame one.

## The store trailer

`shots/trailer.sh` builds the whole trailer from `shots/trailer/*.shot` —
render every beat, join them, lay the music under it, write
`shots/out/trailer/newtonia_trailer.mp4` (1920x1080, H.264 + AAC, which Steam,
YouTube and Reddit all accept without transcoding complaints).

```sh
shots/trailer.sh                        # the whole thing, ~4 min headless
NEWTONIA_TRAILER_SIZE=1280x720 shots/trailer.sh   # faster proof cut
NEWTONIA_TRAILER_KEEP=1 shots/trailer.sh          # keep per-beat mp4s + frames
```

The beats are just clip scenes, assembled in filename order, so re-cutting the
trailer is renaming files and changing one beat re-renders only that beat.
Nothing in `trailer.sh` knows anything the clip harness does not.

| Beat | Shows |
|------|-------|
| `01_open` | Cold open: the lance mirror-bouncing. No title, no caption |
| `02_title` | NEWTONIA over a bare starfield |
| `03_gameplay` | A real generated level, rotate camera — the player's own view |
| `04_specials` | The special asteroid types, captioned |
| `05_pulsar` | Charge, fire, and the shockwave shoving the ship |
| `06_shock` | Chain lightning hopping a ladder of rocks |
| `07_lategame` | Natural generation 14: station, black hole, full hazard counts |
| `08_coop` | Split-screen co-op, both pilots alive |
| `09_end` | End card: the free browser build as the call to action |

The music is the game's own 16 s pause theme (`audio/pause.wav`), looped under
the cut and faded at both ends — it loops rather than stretches, so the cut
can be any length without the tempo drifting.

Two things the script does deliberately: it re-encodes each beat at a fixed
frame rate and GOP so the concat demuxer joins them without artefacts at the
seams, and it takes each beat's frame interval from the harness's own log
rather than assuming, exactly as `gif.sh` does.

## Mobile store screenshots (touch OSD)

`NEWTONIA_FORCE_TOUCH=1` renders any scene with the real touch UI — the
virtual joystick, fire/mine buttons, pause button, and the touch HUD
variant (same layout code the devices run; the desktop OSD guard is
runtime now, see `touch_osd_enabled()`). `shots/mobile.sh` (Linux/WSL)
and `shots/mobile.ps1` (Windows; needs a display at least as big as the
shot) render the store screenshot scenes at Apple's required 6.9" iPhone
(2868x1320) and 13" iPad (2752x2064) landscape sizes plus Play Store
phone/tablet sizes into `shots/out/mobile/`. The split-screen co-op scene
is deliberately excluded — local split screen doesn't exist on the touch
platforms. The iPhone's extra-wide aspect can show toroidal wrap twins of
off-centre rocks in the small early-level worlds; if one bothers you,
nudge that scene or crop.
