# Xbox work is deferred to a private repo

Xbox **console** (GDKX) development is under NDA, so GDKX-touching work has to
live in a **private** repository. As of **2026-07-30 the whole remaining Xbox
effort is deferred there** — not just the NDA parts. `cradle/newtonia` is
public and stays that way for the game; its Xbox surface is frozen at what is
already checked in and green in CI. This doc is the authority on who owns
what.

## Decision (2026-07-30): the private repo owns every outstanding Xbox task

The public repo's Xbox scaffolding builds, is CI-gated on every push, and
needs no further work here. Everything on the remaining-work lists moves to
`cradle/newtonia-xbox`, **including the parts that carry no NDA material at
all**. Rationale: the work is one port, and every item on it is either gated
on GDKX/a dev kit or is only worth doing in service of something that is —
splitting a single effort across two repos by NDA sensitivity costs more
coordination than the split saves, and it leaves the public repo carrying task
lists nobody is working. Ownership follows the *project*, not the sensitivity
of each line.

Consequence for this repo: the `xbox/*.md` documents are **reference and
handoff material, not task lists**. They are accurate descriptions of where
the port stands and what remains; nothing in them is scheduled work for
`cradle/newtonia`.

**The one open public-side action is standing up the private repo** (the
recipe is below) — it gates everything else, and it is a Phase 0 item in
`xbox/PORT_PLAN.md`.

### What moved

| Deferred work | Documented in | Gate |
|---|---|---|
| Program prerequisites: Partner Center title + identity/StoreId, GDKX download, dev-kit loan, publisher name, IARC ratings, pricing | `PORT_PLAN.md` §3 Phase 0, `PARTNER_CENTER_VALUES.md` | Microsoft / calendar |
| GDK Desktop manual test pass (sections B, C, E–K unrun) | `DESKTOP_TEST_PASS.md` | none — a Windows PC; deferred by ownership, not by blocker |
| Console rendering decision spike (GLon12 vs ANGLE-on-GDKX vs native D3D12.X) | `PORT_PLAN.md` §3 Phase 2 | GDKX + dev kit |
| Console bring-up: entry point, input, audio, file I/O, performance | `CONSOLE_BRINGUP.md`, `PORT_PLAN.md` §3 Phase 3 | GDKX + dev kit |
| Cert feature work: PLM API verification against real GDKX headers, `XUser` sign-in/sign-out, `XGameSave`/`SaveStorage`, cert behaviour sweep | `PORT_PLAN.md` §3 Phase 4 | GDKX + dev kit |
| Packaging, CI and store: self-hosted GDKX runner, `deploy-xbox.yml` rework, StoreBroker-vs-`.xvc` verification, real store art, Partner Center secrets | `PORT_PLAN.md` §3 Phases 5–6, `PARTNER_CENTER_VALUES.md` | GDKX (+ Partner Center for the secrets) |
| GDK Achievements Manager backend + Partner Center achievement config + sandbox testing | `ACHIEVEMENTS.md` §6 | GDKX; already private before this decision |
| Xbox console netplay: X1 libdatachannel-on-console spike, X3 dev-mode co-op, X4 store cert | `NETPLAY_XBOX.md` §6 | GDKX + dev kit (X1 gates the rest) |
| Xbox-driven netplay hardening: host-side sanity bounds on client-authoritative pose/fire (8c), malformed/invalid-auth fuzzing of `Net::Reader` (8d) | `NETPLAY_XBOX.md` §7 | none — shared code; see "flowing back" below |

### What the public repo keeps (frozen, no work planned)

- `xbox/CMakeLists.txt`, `xbox_main.cpp`, `xbox/sdl_gdk_stubs.cpp`,
  `xbox/smoke_stubs/`, `xbox/MicrosoftGame.config`, `xbox/PackagingLayout.xml`,
  `xbox/Assets/` + `generate_assets.py`, and the disabled
  `deploy-xbox.yml` template.
- The two CI canaries, which are free and catch real regressions in shared
  code: `xbox-dev.yml` (GDK **Desktop**, Ninja + BWOI of the *public* GDK) and
  `xbox-console-smoke.yml` (`_GAMING_XBOX` under `WINAPI_FAMILY_GAMES`, GDKX
  headers stubbed). **Keep both green** — that is the public repo's whole
  ongoing Xbox obligation.
- The `_GAMING_XBOX` / `_GAMING_DESKTOP` branches already in shared files
  (SDL/EGL/standard Win32 — not NDA), and the platform-neutral seams the
  private backends land against: `Achievements`, `Presence`, `Invites`,
  `net_identity`, `net_policy`, `Overlay::SAFE_AREA_SCALE`, `asset_path()`.
- These docs, as reference.

A shared-code change made for another platform may still touch the Xbox paths
— that is normal, and the two canaries are how it stays honest. What the
public repo does **not** do is schedule Xbox port work.

### Shared-code work, and how it flows back

Some deferred items (8c, 8d, any seam a console backend needs) are plain
cross-platform C++ with no NDA content. The private repo **drives** them
because they are Xbox-motivated; when one is done it comes back as an ordinary
PR against `cradle/newtonia`, per the day-to-day flow below. If some other
platform wants one first, it can simply be done here — nothing about the
deferral makes shared code private.

> The NDA terms are the real authority on what must *never* be public.
> Everything above is an ownership decision layered on top of that; confirm
> the NDA scope with your ID@Xbox contact.

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

Then move the task lists across: the checklists in `DESKTOP_TEST_PASS.md` and
`CONSOLE_BRINGUP.md` are meant to be *filled in* there, and their results
tables are private records (dev-kit output is NDA material). The copies here
stay as the frozen reference.

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
