# Steam Cloud (Auto-Cloud)

Newtonia uses **Steam Auto-Cloud** to sync save progress and high score across
machines. Auto-Cloud is configured entirely on the Steamworks partner site —
there is **no code in the game**. Steam syncs the matching files in the SDL
preference directory on game launch/exit.

> **Status:** verified working end-to-end on Windows, macOS, and Linux
> (incl. Steam Deck, native build). Steam **AppID: 4536720**.

## What is synced

All persisted files live in one directory, returned by
`SDL_GetPrefPath("cc.gfm", "newtonia")` (see `savegame.cpp`, `highscore.cpp`,
`preferences.cpp`). That resolves per-OS to:

| OS      | Directory                                          |
|---------|----------------------------------------------------|
| Windows | `%APPDATA%\cc.gfm\newtonia\` (Roaming)             |
| macOS   | `~/Library/Application Support/cc.gfm/newtonia/`    |
| Linux   | `$XDG_DATA_HOME/cc.gfm/newtonia/` (falls back to `~/.local/share/cc.gfm/newtonia/` when `XDG_DATA_HOME` is unset) |

We sync **only the two files that represent player progress**:

| File              | Synced? | Why                                                       |
|-------------------|---------|-----------------------------------------------------------|
| `savegame.dat`    | ✅      | In-progress game; what players care about carrying over.  |
| `highscore.dat`   | ✅      | High score; should follow the player.                     |
| `preferences.ini` | ❌      | Mixes per-user settings with **per-system** display/window settings (`fullscreen`, `window_width`, `window_height`). Syncing window geometry across machines opens the game off-screen or mis-sized, so the whole file stays local. |

The binary save format is **cross-platform-safe** between these targets: all are
x86-64 little-endian and `savegame.cpp` does field-by-field I/O with fixed-width
types, so a save written on one OS loads on another. (A Steam Deck save and a
Windows save are interchangeable.)

> If we ever want key bindings / sensitivity to roam, the clean fix is to split
> the per-system display settings out of `preferences.ini` into a separate,
> never-synced local file (e.g. `display.ini`) and then add the remainder to the
> Auto-Cloud config. Until then, preferences are intentionally machine-local.

## Quota

Auto-Cloud and the ISteamRemoteStorage API draw from the **same** per-app Cloud
quota (byte + file-count), set in the dashboard. Our files are tiny
(`savegame.dat` a few KB, `highscore.dat` 4 bytes), so quota is never a concern.
Set the dashboard quota generously (a few MB / 100+ files) to leave headroom for
future save data. A quota of **0 silently syncs nothing** even though everything
else looks configured — make sure it is non-zero.

## Steamworks dashboard configuration

The correct mental model is **one file config, then per-platform root
overrides** — *not* a pile of independent per-platform rows. You define each
cloud file once (its `path` + `pattern`), and then tell Steam how to locate that
same logical file on each OS by overriding the **root**.

**Steamworks → your app → Application → Cloud**

1. **Enable Steam Cloud** for the app and set non-zero byte/file quotas.
2. **Define the file rules once** (shared across platforms): `path` =
   `cc.gfm/newtonia`, one rule per file with the literal pattern `savegame.dat`
   and `highscore.dat`.
3. **Add a root override per platform** so that shared `path` resolves to the
   real SDL pref directory on each OS:

| Platform | Root override       | `path`           | resolves to                                  |
|----------|---------------------|------------------|----------------------------------------------|
| Windows  | `WinAppDataRoaming` | `cc.gfm/newtonia`| `%APPDATA%\cc.gfm\newtonia\`                 |
| macOS    | `MacAppSupport`     | `cc.gfm/newtonia`| `~/Library/Application Support/cc.gfm/newtonia/` |
| Linux    | `LinuxXdgDataHome`  | `cc.gfm/newtonia`| `~/.local/share/cc.gfm/newtonia/`            |

4. **Publish to the public (Default) branch.** See the gotcha below — saving or
   publishing to a dev/beta branch only does *not* reach normal installs.

### Patterns

- **Literal patterns** (`savegame.dat`, `highscore.dat`) mean only those two
  files sync; `preferences.ini` never matches and stays local — exactly the
  per-file control we want.
- **Shortcut:** the glob `*.dat` matches both files and excludes the `.ini`.
  Prefer the explicit list for intent-clarity; if you ever add another `.dat`
  file you don't want synced, the glob would grab it but the explicit list won't.

### Linux / Steam Deck root pairing (this is the easy one to get wrong)

The Linux **root** and **path** must agree, or Steam matches **0 files** and
sync silently does nothing. SDL builds the pref path as
`$XDG_DATA_HOME/cc.gfm/newtonia/` (verified in SDL's
`unix/SDL_sysfilesystem.c`), falling back to `~/.local/share/cc.gfm/newtonia/`.

| Root           | resolves to                | required `path`                |
|----------------|----------------------------|--------------------------------|
| `LinuxXdgDataHome` | `$XDG_DATA_HOME` (≈ `~/.local/share`) | `cc.gfm/newtonia` ✅ |
| `LinuxHome`    | `$HOME`                    | `.local/share/cc.gfm/newtonia` ✅ |
| `LinuxHome`    | `$HOME`                    | `cc.gfm/newtonia` ❌ → `~/cc.gfm/newtonia` (matches nothing) |

The classic bug is reusing the Windows/macOS path (`cc.gfm/newtonia`) under the
`LinuxHome` root, which drops the `.local/share` segment. Use `LinuxXdgDataHome`
+ `cc.gfm/newtonia` (recommended), or `LinuxHome` + `.local/share/cc.gfm/newtonia`.
Root enum labels can vary slightly by Steamworks SDK version; confirm them when
entering.

**Steam Deck specifics**

- These instructions assume the **native Linux build**. Save lives at
  `~/.local/share/cc.gfm/newtonia/` (`$HOME` = `/home/deck`, `XDG_DATA_HOME`
  unset on SteamOS, so the fallback path is used).
- If the Deck runs the **Windows build under Proton** instead, the save goes
  inside the Proton prefix
  (`…/steamapps/compatdata/4536720/pfx/drive_c/users/steamuser/AppData/Roaming/cc.gfm/newtonia/`)
  and the existing **`WinAppDataRoaming`** rule maps into it automatically — do
  **not** rely on the Linux root override in that case. Check the game's
  **Properties → Compatibility** to see which mode it is in.
- Path/root resolution is **identical between Desktop Mode and Game Mode** — no
  separate config. The only Game-Mode difference is timing: see conflicts below.

## Client-side prerequisites

Even with the config published, Cloud will not sync unless:

- The app-level **"Enable Steam Cloud"** master toggle is on (separate from, and
  required by, the Auto-Cloud root overrides).
- Account-wide Cloud is on: **Steam → Settings → Cloud**.
- In the current Steam client there is **no standalone "Cloud" page** under a
  game's Properties. Per-game Cloud appears as a checkbox on the **General** tab
  (only when the backend has Cloud enabled), and cloud usage is listed under
  **Steam → Settings → Storage**.

## Conflicts (the "it uploaded but didn't download" trap)

Auto-Cloud uploads on a clean game **exit** and downloads on the next clean
**launch**, but only when it can prove the local copy is an ancestor of the
cloud copy. When both the local and cloud `savegame.dat` have changed
independently, Steam treats it as a **conflict and keeps the local file** — so a
save uploaded from device A never appears on device B. In `cloud_log.txt` this
shows as:

```
Need to sync from local change number 'N' to global change number 'N+1' (full), but not attempting now
Skipping download of file cc.gfm/newtonia/savegame.dat to our platform
```

Causes and avoidance:

- **Launching on the target device before the source finished uploading.** The
  target loads its stale local save, auto-saves, and re-uploads — burying the
  newer save. Let the source device finish its cloud upload (back out to the
  library; don't just **suspend** the Steam Deck) before playing elsewhere.
- **Offline mode / hard kills** leave divergent saves. Always exit cleanly while
  online.
- When Steam *does* prompt a **"Cloud Sync Conflict"** dialog, pick the newer
  device; don't habitually choose local.

**Recovery (proven):** on the device that is behind, delete the local
`savegame.dat`, then relaunch through Steam. With no local file there is no
conflict, and Steam downloads the authoritative cloud copy.

## Troubleshooting / verification

Steam's own cloud log is the authoritative source — it shows exactly which rules
matched, how many files, and every up/download:

| Platform | `cloud_log.txt` location |
|----------|--------------------------|
| Windows  | `C:\Program Files (x86)\Steam\logs\cloud_log.txt` |
| Linux / Steam Deck | `~/.local/share/Steam/logs/cloud_log.txt` |
| macOS    | `~/Library/Application Support/Steam/logs/cloud_log.txt` |

What to look for (AppID 4536720):

- `Evaluating rule N … pattern="savegame.dat"` followed by
  `Found 1 files that match …` → the root/path is correct.
  `Found 0 files` → root/path mismatch (see the Linux table above).
- `Upload OK for file cc.gfm/newtonia/savegame.dat` → upload succeeded.
- `Download OK for file cc.gfm/newtonia/savegame.dat` → download succeeded.
- `but not attempting now` / `Skipping download … to our platform` → a conflict
  (see above), or the local copy is already current.

Quick end-to-end test:

1. Play far enough to auto-save (pause, death with lives/score remaining, or a
   level-up), then **fully exit** through Steam. Confirm `Upload OK` in the log.
2. Confirm cloud usage under **Steam → Settings → Storage**.
3. On a second machine — or on the same one after deleting the local
   `savegame.dat` — launch through Steam and confirm a real `Download OK` (not
   `Skipping … File is in sync`); progress and high score should restore.

## Gotcha checklist (in the order they bit us)

1. **Master "Enable Steam Cloud" toggle was off** — Auto-Cloud rows do nothing
   and no Cloud UI appears client-side until it is on.
2. **Config published to dev only** — must be published to the **public
   (Default) branch** to reach normal installs.
3. **Linux/Steam Deck root/path pairing** — `LinuxHome` + `cc.gfm/newtonia`
   matched 0 files; needs `LinuxXdgDataHome` + `cc.gfm/newtonia`.
4. **Cross-device conflict keeps local** — uploaded saves don't download while a
   diverging local save exists; delete-local to recover, and sync cleanly
   (don't just suspend the Deck) to avoid.
