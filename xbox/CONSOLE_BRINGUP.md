# Console bring-up — Phase 3 checklist

> **DEFERRED (2026-07-30): this checklist is executed in the private repo
> `cradle/newtonia-xbox`,** which owns all remaining Xbox work — see
> `xbox/PRIVATE_REPO.md`. Fill it in there, not here: it needs GDKX and
> console hardware, and dev-kit output (logs, captures) is NDA material that
> must not land in the public repo. The copy below is the frozen reference.

Validates Newtonia running on an **Xbox Series X|S** target (a dev kit, or a
retail console in GDK Developer Mode). Companion to `DESKTOP_TEST_PASS.md`
(which covers the GDK *Desktop* canary) and `PORT_PLAN.md` Phase 3.

## Prerequisites

- [ ] GDKX installed on the build machine (NDA; from your ID@Xbox account).
- [ ] A console target: retail Series X|S in Developer Mode **or** a dev kit,
      activated/registered to your ID@Xbox account.
- [ ] **Phase 2 rendering decision made** — the console build must produce a
      binary that can actually present to the Xbox GPU. ANGLE.WindowsStore is
      desktop-D3D11; the console needs D3D12.X. Section B is where this is
      proven, so don't expect a picture until Phase 2 lands.
- [ ] A Scarlett build path in `xbox/CMakeLists.txt` (Ninja, `_GAMING_XBOX`,
      GXDK `xbox/` includes+libs) — see `deploy-xbox.yml` Configure TODO.

Capturing output on console (there is no `newtonia.log` like Desktop):
view `SDL_Log`/`OutputDebugString` via the GDK debugger (Visual Studio GDK
debug session) or `xbtools` console output; deploy with VS "Deploy" or
`xbapp install`, launch with `xbapp launch`.

## A. Build, package, deploy

- [ ] `Gaming.Xbox.Scarlett.x64` build completes (Ninja + GDKX).
- [ ] `makepkg` produces an `.xvc`/loose package from the layout
      (`PackagingLayout.xml` + `MicrosoftGame.config` with identity injected
      from secrets).
- [ ] Package installs on the console (`xbapp install` / VS Deploy) with no
      manifest/cert errors.

## B. First launch & rendering  ← the critical milestone

- [ ] Title launches from the console dashboard; no immediate crash.
- [ ] Splash screen shows, then the menu **renders** (starfield + text). This
      is the proof the rendering backend works on the console GPU.
- [ ] In-game world renders: ships, asteroids, particles, HUD.
- [ ] Invisible-asteroid lensing (`WarpPass`, generation ≥ 4) renders with no
      GPU errors.
- [ ] No GL/EGL errors in the debug log during a few minutes of play.

## C. Controller input (the only input on console)

- [ ] Menu fully navigable by pad (D-pad/A/Start), options screen adjustable.
- [ ] All ship actions work (analog stick, R2 shoot, L2 mine, X/Y weapons,
      LB boost, RB teleport, R3 help overlay).
- [ ] Help overlay shows controller glyphs (not keyboard labels).
- [ ] Pause = START; quit-to-menu = BACK.
- [ ] Second controller → player 2 joins with START (split-screen).
- [ ] Controller disconnect mid-game auto-pauses; reconnect resumes (and a
      dead/game-over player's disconnect does NOT pause the survivor).

## D. Audio

- [ ] All effect classes play (shoot, explosion, thud/ting, pickup, boost,
      shield, mine, level-clear ticks, god-mode music) at 48 kHz.
- [ ] No dropouts during heavy combat.

## E. Save / preferences / high score

- [ ] Options changes persist across relaunch (`SDL_GetPrefPath` → the title's
      persistent local storage on console).
- [ ] Auto-save on pause/death; resume restores score, lives, generation.
- [ ] State survives a **full console reboot** (persistent storage, not just
      session). If it doesn't, cert likely needs `XGameSave` — see PORT_PLAN
      Phase 4.

## F. Asset loading

- [ ] `audio/` files load from the installed package (verify `asset_path()`
      resolves under the package mount; if working dir ≠ install root, confirm
      `SDL_GetBasePath()` prefixing works on console).

## G. PLM / lifecycle (cert-critical — needs a dev kit for full coverage)

- [ ] Suspend the title (dashboard/Quick Resume): `plm_suspend_callback`
      saves progress and acknowledges within the time budget.
- [ ] Resume: game continues without a timing jump (`s_reset_tick` discards
      the stale delta — no teleporting objects).
- [ ] Quick Resume after a real reboot restores the running session.
- [ ] **Verify the PLM API itself** against real GDKX headers —
      `XSuspendResume*` is stubbed/assumed in CI (PORT_PLAN Phase 4); confirm
      the registration actually compiles and fires on hardware.
- [ ] Constrained mode (background/overlay) handled like focus loss.

## H. TV safe area

- [ ] On a real TV, all HUD/menu UI sits inside the title-safe region
      (`Overlay::SAFE_AREA_SCALE` = 0.9 on `_GAMING_XBOX`); nothing clipped.

## I. Performance

- [ ] 1080p (S-class profile): smooth in normal play.
- [ ] 4K (X profile): profile late-generation worlds + warp pass; if the
      full-viewport warp copy blows the frame budget, fall back to an internal
      render scale (PORT_PLAN Phase 3).

## J. User sign-in (if implemented — Phase 4)

- [ ] Silent default-user sign-in at boot (`XUserAddAsync`).
- [ ] Sign-out mid-game handled gracefully (pause / return to menu).

## K. Stability & cert sweep

- [ ] No unintended process-exit paths; menu "quit" exits cleanly.
- [ ] Controller-disconnect messaging present where required.
- [ ] No keyboard requirement anywhere (already true).
- [ ] Run the GDK certification test suite; record findings.

## Results

| Date | Tester | GDKX ed. | Build (commit) | Console (kit/dev-mode) | Result | Notes |
|------|--------|----------|----------------|------------------------|--------|-------|
|      |        |          |                |                        |        |       |
