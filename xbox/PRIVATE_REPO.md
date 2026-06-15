# Private repo for Xbox console development

Xbox **console** (GDKX) development is under NDA, so the GDKX-touching work
must live in a **private** repository. `cradle/newtonia` is public and stays
that way for the game and all non-NDA work. This doc records how to split the
work and stand up the private repo.

## What stays public vs goes private

Split by **NDA sensitivity**, not "all Xbox = private". Most of the Xbox work
so far is public-safe — it uses only the *public* GDK, deliberately stubs
GDKX (`xbox/smoke_stubs/`), and commits no SDK bytes.

**Stays in the public repo (`cradle/newtonia`):**
- The port plan, checklists, and this doc.
- GDK **Desktop** CI (`xbox-dev.yml`, Ninja + BWOI of the *public* GDK).
- Console API-partition **compile-smoke** (`xbox-console-smoke.yml`,
  `WINAPI_FAMILY_GAMES` + `_GAMING_DESKTOP` — the desktop-GL/GLon12 renderer
  checked against the Game OS partition; no GDKX, nothing stubbed).
- The `_GAMING_XBOX` / `_GAMING_DESKTOP` code already in shared files
  (SDL/EGL/standard Win32 — not NDA).
- Identity-via-secrets plumbing, the `deploy-xbox.yml` template.

**Goes to the private mirror only:**
- Real GDKX API usage: D3D12.X / console rendering (Phase 2), and real
  `XGameSave` / `XUser` / PLM against actual GDKX headers (Phase 4).
- Console build config that references GDKX specifics; the Scarlett toolchain
  if it embeds GDKX details.
- Anything off the dev kit: logs, crash dumps, perf captures (most clearly
  NDA-restricted).

> The NDA terms are the real authority on what must be private. Confirm the
> exact scope with your ID@Xbox contact; move the line above accordingly.

## Why a mirror, not a submodule

A submodule only works when the private code is cleanly *separable* (a private
parent + the public game as a read-only sub-repo). Our Xbox code is **woven
into shared files** (`xbox_main.cpp`, `gles2_compat`, `glgame.cpp`,
`asset_path.h`, …), and the future GDKX work edits shared files too
(`savegame.cpp` save-storage seam, the PLM block). You can't patch a
submodule's files from its parent, so a submodule would force forking those
files anyway. A full private mirror keeps one tree, one build, one toolchain.

## Setup: private mirror with upstream tracking

You can't make a public-repo *fork* private (forks of a public repo are
public). Use the bare-clone mirror recipe:

```sh
# 1. Create an empty PRIVATE repo: cradle/newtonia-xbox

# 2. Mirror the public repo into it (one time)
git clone --bare https://github.com/cradle/newtonia.git
git -C newtonia.git push --mirror https://github.com/cradle/newtonia-xbox.git
rm -rf newtonia.git

# 3. Working clone of the private repo, with the public repo as upstream
git clone https://github.com/cradle/newtonia-xbox.git
cd newtonia-xbox
git remote add upstream https://github.com/cradle/newtonia.git
```

## Day-to-day flow

- **Public changes flow down** into the private repo:
  ```sh
  git fetch upstream
  git merge upstream/master      # or rebase
  ```
- **Non-NDA improvements made in the private repo flow up**: cherry-pick or
  branch them and open a PR against `cradle/newtonia`. Keep NDA commits out of
  those branches.
- **NDA work** is committed and pushed only to `origin` (the private mirror),
  never to `upstream`.

## Hygiene (private repo too)

- **Never commit the GDK/GDKX SDK itself** — it's installed on the build
  machine, not vendored (same as the public GDK today).
- **Keep identity out of source** — it's injected from GitHub secrets
  (see `xbox/PARTNER_CENTER_VALUES.md`); applies in the private repo as well.
- **GDKX CI runs on a self-hosted runner only** — never a public hosted
  runner; GDKX bytes must never reach logs, caches, or artifacts.
- Double-check `upstream` before pushing: `git remote -v`. NDA branches go to
  `origin` (private), never `upstream` (public).
