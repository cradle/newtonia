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
players 2              # local split-screen co-op (1..4; 3-4 use the 2x2 grid)

ship 0 -150 25         # move player 1 (and the camera) / heading in degrees
                       #   (0 = up, positive = counter-clockwise)
ship2 300 0 -30        # move player 2 (ship3/ship4 likewise with `players 3|4`)

# TYPE flags combine; r= clamps to Asteroid::max_radius; v= units/second.
asteroid X Y [normal|invincible|invisible|reflective|teleporting|quantum|
              tough|armoured|phasing]... [r=R] [v=VX,VY]
enemy X Y [difficulty]         # AI ship (it will fight!)
hazard pulsar|comet|seeker X Y [v=VX,VY]   # a natural comet cruises ~280 u/s
blackhole X Y
pickup TYPE X Y        # weapon mine giga missile shield god nova beam
                       #   lance shock revive life timeslow

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

## Video capture (`shots/video.sh`)

The same idea one dimension further: instead of composing a scene and
capturing one instant, **render a recorded replay** (REPLAY.md) frame by
frame to an MP4. That's a gameplay video of a real run — the store page's
trailer footage — rebuildable from the command line whenever the game
changes.

```sh
shots/video.sh --info                             # what's in best.nrp?
shots/video.sh                                    # best.nrp -> shots/out/gameplay.mp4
shots/video.sh --replay recent --out promo.mp4
shots/video.sh --start 0:30 --duration 1:00       # trim to a highlight
shots/video.sh --no-hud --size 2560x1440          # pure world, 1440p
```

Pick the run first — a replay file is a whole run, and a trailer wants a
minute of it. `--info` prints the header (score, level reached, length),
`--start`/`--duration` take `90`, `1:30` or `500ms`. Whatever you cut,
the harness skips ahead **without rendering**, so trimming makes the
render shorter rather than throwing frames away afterwards.

Defaults worth knowing: 1920x1080 at 60 fps, the in-game HUD ON (a store
video usually wants the score and lives) and the REPLAY watermark,
timeline and control hints OFF — `--chrome` puts them back, `--no-hud`
takes the HUD and minimap away too.

**This is a render, not a screen recording.** Frame N is always exactly
one frame time after frame N-1, whatever the machine managed, so a slow
headless GL context produces a smooth 60 fps video rather than a stuttery
one — and the same replay renders the same frames every time (the e2e
driver asserts that byte for byte). Under Xvfb expect 1080p to run well
below real time; it is still every frame.

Picture and sound are **two passes over the same replay**, run in
parallel, because they cannot be one: SDL calls the audio callback with
the mixer lock held and the game takes that lock on every sound call, so
pacing the mixer from inside the callback deadlocks the game thread. The
video pass renders as fast as it can with no audio; the audio pass walks
the same records with the same fixed timestep, never draws, and throttles
itself to the audio device's real-time rate — so it costs the clip's own
duration, and the two line up by construction. The script muxes them
(x264 CRF 16 + AAC). Each pass reports its numbers; the audio pass also
prints a **sync margin** (one mixer buffer, ~23 ms, is the floor) and
warns if the sim ever fell behind the device, which is the one condition
that would drift sound against picture.

**Two things about the output that surprise.** Star fields are the worst
case h264 has: thousands of tiny high-contrast points, all moving, all
different every frame. The opening 90 s of a real run came out at 177 MB at
the default CRF 16 (visually lossless, the right default for a master you
hand to a store) — `--crf 20` roughly halves it and `--crf 24` roughly
quarters it, with the stars the first thing to go soft. And the audio pass
is the only REAL-TIME half of the render, so it is the one to protect: the
script nices the video pass and the encoder below it, because saturating
every core measured a 135 ms stall in the paced sim where the same window
rendered alone measured 23 ms. If the pass warns about a stall anyway, free
up the machine and re-run it — a smaller `--size` will not help, that pass
never draws.

**Uploading it somewhere.** Every platform re-encodes what you give it, so
hand over the harness's own output and never a re-encode of it — two lossy
passes over a starfield is where the stars turn to mush. That is also the
argument for rendering ABOVE the resolution you're targeting: YouTube gives
1440p and 2160p uploads a better codec and a far bigger bitrate allowance
than 1080p, which this game's content needs more than most. `--size
2560x1440` costs about 1.7x the render time of 1080p (10 s took 40 s of wall
clock on a 4-core software-GL box, so a 90 s clip is a few minutes) and a
correspondingly bigger file. The muxed audio is AAC 320k — cheap next to the
video, and a master should not be the place quality is saved.

**On Windows use `shots\video.ps1`, not video.sh.** The POSIX script streams
frames through a fifo, and MSYS2's fifos are emulated for MSYS2-linked
programs — a native `newtonia.exe` cannot open one, so that script cannot work
there however it is invoked. The PowerShell twin takes the same options
(`-Replay`, `-Start`, `-Duration`, `-Size`, `-Crf`, `-NoHud`, `-Chrome`,
`-Info`), downloads a leaderboard `season/run_id` for you, and uses
`NEWTONIA_VIDEO=-` — frames on stdout, piped to ffmpeg **through cmd**,
because a PowerShell pipeline decodes bytes as text and would destroy them.
It also runs the two passes one after the other rather than together: the
audio pass is the real-time half and starving it costs sync.

**There is also a CI route, parked.** `.github/workflows/disabled/video.yml`
builds, renders and uploads the mp4 as a downloadable artifact — no build, no
bash, no ffmpeg, no GPU at the other end, which is the answer for a machine
that cannot build the game. It is kept in `disabled/` rather than live because
artifact storage is a 2 GB quota shared with every other workflow in this repo
and a 90 s 1440p CRF 16 render is ~240 MB of it; a handful of trailers would
fill the account. Move the file into `.github/workflows/`, render what you
need, download it, delete the artifact, and move the file back. It keeps
artifacts for one day and prints their size and share of the quota into the
job summary. `crf` is the cheap lever if you do need it live: 20 roughly
halves the file, 24 roughly quarters it.

Rendering locally costs no quota at all, so that is the habit worth having.

Direct control, if you'd rather not use the script — `NEWTONIA_VIDEO`
(rgb24 frame stream; a fifo works, and `-` means stdout), `NEWTONIA_VIDEO_AUDIO` (s16 mix; the
other pass), `NEWTONIA_VIDEO_REPLAY`, `_SIZE`, `_FPS`, `_START_MS`,
`_MS`, `_HUD`, `_CHROME`, `_SEED`, `_INFO`. Full notes in
`video_capture.h`.

Like shot mode, a capture touches no player data: no Steam, no
achievements, no preference writes, no saves (playback already writes
nothing). Verified end to end by `test/e2e/video.sh`.
