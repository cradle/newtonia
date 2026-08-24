// UI chrome for the Newtonia web port.
// Handles fullscreen, mute, and on-screen touch controls.
// Compiled to main.js via tsc and loaded after the Emscripten script.

declare const Module: {
  setStatus(text: string): void;
  onRuntimeInitialized?: () => void;
};

(function () {
  const canvas = document.getElementById("canvas") as HTMLCanvasElement;
  const fsBtn = document.getElementById("fullscreen-btn") as HTMLButtonElement;
  const muteBtn = document.getElementById("mute-btn") as HTMLButtonElement;

  // ---- Universal join link (?code=) ----
  // A shared https://newtonia.metonymous.com/join?code=XXXX link that fell
  // through to the web game (no native app installed, or a desktop browser)
  // lands here with the room code in the query string. Hand it to the wasm
  // module once its runtime is up (Module._web_accept_invite is exported),
  // then scrub ?code= from the URL so a refresh doesn't rejoin. Menu::tick
  // polls Invites::poll_accepted_invite and jumps into the lobby as a joiner.
  (function handleJoinCode() {
    const code = new URLSearchParams(window.location.search).get("code");
    if (!code) return;
    const m = Module as any;
    let tries = 0;
    const deliver = () => {
      if (m.ccall && m._web_accept_invite) {
        m.ccall("web_accept_invite", null, ["string"], [code]);
        const clean = window.location.pathname + window.location.hash;
        window.history.replaceState({}, "", clean);
        return;
      }
      if (tries++ < 200) setTimeout(deliver, 50);  // ~10 s of runtime warmup
    };
    deliver();
  })();

  // ---- Leaderboard watch link (?replay=<season>/<run_id>) ----
  // A WATCH link on the site's /leaderboard/ page opens the game with
  // ?replay=. Download the blob from the board worker's public /replay/
  // endpoint, then hand the bytes to the wasm module once its runtime and
  // IDBFS are up (web_watch_replay stages download.nrp and Menu::tick
  // starts the ordinary downloaded-replay playback). The param is kept in
  // the URL — the link is a shareable, refresh-to-rewatch view.
  (function handleReplayLink() {
    const q = new URLSearchParams(window.location.search);
    const spec = q.get("replay");
    if (!spec) return;
    // <season>/<run_id>: season is the header's stamp (printable ASCII, no
    // space/slash/backslash, <= 23 chars — the worker re-validates), run_id
    // decimal. Split first, then validate each part — a class-based test on
    // the whole spec would let a stray extra slash through.
    const parts = spec.split("/");
    if (parts.length !== 2) return;
    const [season, run] = parts;
    // Dot segments are printable ASCII, so the class test alone lets `..`
    // through, and encodeURIComponent does not escape `.` — the fetch URL
    // then normalises to a different path on the worker (same origin, so
    // never an SSRF, but it should not be attempted at all).
    if (!/^[!-~]{1,23}$/.test(season) || season.indexOf("\\") !== -1 ||
        season === "." || season === ".." ||
        !/^[0-9]{1,20}$/.test(run)) return;
    const host = q.get("board") === "beta"
        ? "https://newtonia-board-beta.gfmcc.workers.dev"
        : "https://newtonia-board.gfmcc.workers.dev";
    // The reader's own web ceiling (REPLAY.md). Bounding the download
    // matters for more than politeness: the body is materialised three
    // times over (JS ArrayBuffer, the wasm heap copy, then the MEMFS file),
    // wasm memory never shrinks once grown, and an unbounded body is what
    // makes a failed _malloc reachable at all.
    const MAX_REPLAY_BYTES = 48 * 1024 * 1024;
    const tooBig = () => {
      alert("That replay is too large for the web build to play.");
    };
    fetch(`${host}/replay/${encodeURIComponent(season)}/${run}.nrp`)
      .then((r) => {
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        // Content-Length can lie or be absent (and says nothing about the
        // DECOMPRESSED size), so this is only an early out — the real
        // check is on the decoded bytes below.
        const declared = Number(r.headers.get("content-length"));
        if (declared > MAX_REPLAY_BYTES) {
          tooBig();
          return null;
        }
        return r.arrayBuffer();
      })
      .then((buf) => {
        if (!buf) return;               // already refused above
        const bytes = new Uint8Array(buf);
        if (!bytes.length || bytes.length > MAX_REPLAY_BYTES) {
          tooBig();
          return;
        }
        const m = Module as any;
        let tries = 0;
        const retry = () => {
          if (tries++ < 600) setTimeout(deliver, 100); // ~60 s of warmup
        };
        const deliver = () => {
          if (!(m._web_watch_replay && m._malloc && m._free && m.HEAPU8))
            return retry();
          // Readiness probe that costs no allocation. web_watch_replay
          // answers 0 for "IDBFS still mounting" BEFORE it validates its
          // arguments, and -1 for "ready, bad arguments" after — so a null
          // call distinguishes the two. Retrying the real call instead
          // re-malloc'd and re-copied the whole blob every 100 ms (~9.6 GB
          // of memcpy across a full retry budget).
          if (m._web_watch_replay(0, 0) === 0) return retry();
          let ptr = 0;
          try {
            ptr = m._malloc(bytes.length);
            // A failed allocation returns 0 in this build (ABORTING_MALLOC
            // is off), and set(bytes, 0) would write the blob over wasm
            // address 0 — smashing the static data segments and trapping
            // the game loop every frame thereafter.
            if (!ptr) {
              tooBig();
              return;
            }
            // Read HEAPU8 *after* the malloc, never before: a growth
            // detaches the old buffer and emscripten installs a fresh view
            // on Module. Hoisting this into a variable above the malloc is
            // the obvious refactor and it silently breaks the copy.
            m.HEAPU8.set(bytes, ptr);
            if (m._web_watch_replay(ptr, bytes.length) < 0)
              alert("This replay can't be played by the current web build " +
                    "(it was recorded by a different game version).");
          } catch (e) {
            // Past the first setTimeout this runs outside the promise
            // chain, so without catching here the failure would surface as
            // an uncaught page error and the user would see nothing.
            console.warn("[newtonia] replay hand-off failed:", e);
            alert("This replay could not be loaded.");
          } finally {
            if (ptr) m._free(ptr);
          }
        };
        deliver();
      })
      .catch((e) => {
        console.warn("[newtonia] replay download failed:", e);
        alert("Replay download failed — it may have dropped off the leaderboard.");
      });
  })();

  // ---- Fullscreen ----
  function toggleFullscreen() {
    if (!document.fullscreenElement) {
      const container = document.getElementById("game-container")!;
      container.requestFullscreen().catch(() => {
        // Fallback: fullscreen just the canvas
        canvas.requestFullscreen?.();
      });
      fsBtn.textContent = "⛶";
    } else {
      document.exitFullscreen();
      fsBtn.textContent = "⛶";
    }
  }

  fsBtn.addEventListener("click", toggleFullscreen);

  // The "F" key toggles fullscreen, matching the desktop builds. This must be
  // handled here in a real DOM key event (a valid user gesture) rather than
  // forwarded through the SDL event queue, which the browser rejects for
  // requestFullscreen(). The game has no action bound to "f", so letting the
  // event continue on to the canvas/SDL is harmless.
  window.addEventListener("keydown", (e) => {
    if ((e.key === "f" || e.key === "F") && !e.repeat &&
        !e.ctrlKey && !e.metaKey && !e.altKey) {
      toggleFullscreen();
    }
  });

  document.addEventListener("fullscreenchange", () => {
    fsBtn.title = document.fullscreenElement ? "Exit fullscreen" : "Fullscreen";
    canvas.style.cursor = document.fullscreenElement ? "none" : "";
  });

  // ---- Mute ----
  // We use AudioContext.suspend/resume to silence all SDL2_mixer output.
  // Emscripten's SDL2 port calls ctx.resume() on every user gesture to comply
  // with browser autoplay policy, which would immediately undo a suspend-based
  // mute. To prevent that, we replace ctx.resume with a no-op while muted and
  // restore it when the user unmutes.
  let muted = false;
  let audioCtx: AudioContext | null = null;

  function getAudioContext(): AudioContext | null {
    // SDL2/Emscripten exposes its AudioContext at window.SDL.audioContext.
    // We also check window._newtAudioCtx, which is set by the AudioContext
    // constructor intercept in shell.html (covers the case where the context
    // was created before SDL exports it, or on alternate Emscripten builds).
    const w = window as Window & {
      SDL?: { audioContext?: AudioContext };
      _newtAudioCtx?: AudioContext;
    };
    return w.SDL?.audioContext ?? w._newtAudioCtx ?? null;
  }

  muteBtn.addEventListener("click", () => {
    muted = !muted;
    muteBtn.textContent = muted ? "🔇" : "🔊";
    muteBtn.title = muted ? "Unmute" : "Mute";

    // Lazily capture context; covers clicking mute before audio has started.
    if (!audioCtx) audioCtx = getAudioContext();

    if (audioCtx) {
      if (muted) {
        audioCtx.suspend();
        // Block SDL2/browser from resuming while muted.
        if (!(audioCtx as any)._origResume) {
          (audioCtx as any)._origResume = audioCtx.resume.bind(audioCtx);
          audioCtx.resume = () => Promise.resolve();
        }
      } else {
        const orig = (audioCtx as any)._origResume as (() => Promise<void>) | undefined;
        if (orig) {
          audioCtx.resume = orig;
          delete (audioCtx as any)._origResume;
        }
        audioCtx.resume();
      }
    }
  });

  // ---- On-screen touch controls ----
  // Left half: floating analog joystick that calls Module._web_touch_joystick(nx, ny).
  // Right half: large circular action buttons with press-state visual feedback.
  // Multi-touch is supported — joystick and action buttons track independent fingers.

  const TOUCH_MEDIA = window.matchMedia("(pointer: coarse)");

  // Extend Module type to include our exported C functions.
  type ModuleEx = typeof Module & {
    _web_touch_joystick?(nx: number, ny: number): void;
    _web_menu_tap?(nx: number, ny: number): void;
  };

  function callTouchJoystick(nx: number, ny: number): void {
    (Module as ModuleEx)._web_touch_joystick?.(nx, ny);
  }

  // Module-level refs so setMenuMode can show/hide them.
  let _circleButtonEls: HTMLElement[] = [];
  let _menuOverlay: HTMLElement | null = null;
  let _joyPlaceholderEls: HTMLElement[] = [];
  let _positionJoyPlaceholder: (() => void) | null = null;
  let _inMenuMode = true;
  // The mine button only exists while the local ship has a secondary
  // equipped — C++ pushes changes via EM_ASM (glgame.cpp GLGame::tick),
  // the same bridge setMenuMode rides. Starts false: a fresh ship has no
  // secondary until a pickup grants one.
  let _mineAvailable = false;

  function applyCircleButtonVisibility(): void {
    for (const el of _circleButtonEls) {
      const hide = _inMenuMode ||
          (!_mineAvailable && el.classList.contains("touch-mine"));
      el.style.display = hide ? "none" : "";
    }
  }

  // Called from C++ via EM_ASM when game state changes.
  function setMenuMode(isMenu: boolean): void {
    _inMenuMode = isMenu;
    applyCircleButtonVisibility();
    for (const el of _joyPlaceholderEls) {
      el.style.display = isMenu ? "none" : "";
    }
    if (!isMenu) {
      _resizeFn?.(); // Ensure buttons are correctly sized when they become visible
      _positionJoyPlaceholder?.();
      hideGameOverBanner(); // a new run started — last run's promo is stale
    }
    if (_menuOverlay) _menuOverlay.style.display = isMenu ? "block" : "none";
  }
  (window as any).setMenuMode = setMenuMode;

  // Called from C++ via EM_ASM when the local ship gains its first
  // secondary or fires off its last one.
  function setMineAvailable(available: number | boolean): void {
    _mineAvailable = !!available;
    applyCircleButtonVisibility();
  }
  (window as any).setMineAvailable = setMineAvailable;

  // Called from C++ via EM_ASM while Ship::boost's cooldown runs — the
  // button dims but stays pressable (presses no-op in Ship::boost()).
  function setBoostReady(ready: number | boolean): void {
    const el = document.querySelector<HTMLElement>(".touch-boost");
    if (el) el.classList.toggle("cooldown", !ready);
  }
  (window as any).setBoostReady = setBoostReady;

  // Active-weapon icons on the shoot/mine circles: C++ pushes the selected
  // primary/secondary kinds (Save::WeaponEntry::Kind values; secondary -1 =
  // none) via EM_ASM whenever they change, and the buttons carry a small
  // inline-SVG glyph — the same vocabulary as the native OSD (crosshair
  // gun, violet/amber pickup stars, teal turret ring, ...).
  function weaponIconSvg(kind: number): string | null {
    const S = (color: string, body: string) =>
      `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="1.6">${body}</svg>`;
    const star = (color: string) => S(color,
      '<path d="M12 2 L13.8 10.2 L22 12 L13.8 13.8 L12 22 L10.2 13.8 L2 12 L10.2 10.2 Z"/>');
    const rays = (n: number, r0: number, r1: number, altR1?: number) => {
      let d = "";
      for (let i = 0; i < n; i++) {
        const a = (2 * Math.PI * i) / n;
        const rr = altR1 !== undefined && i % 2 ? altR1 : r1;
        d += `M${12 + r0 * Math.cos(a)} ${12 + r0 * Math.sin(a)} L${12 + rr * Math.cos(a)} ${12 + rr * Math.sin(a)} `;
      }
      return `<path d="${d}"/>`;
    };
    switch (kind) {
      case 0:  // Default gun: crosshair
        return S("#ffffff", '<circle cx="12" cy="12" r="5"/>' + rays(4, 5, 9));
      case 1:  // GodMode: sun
        return S("#ffd94d", '<circle cx="12" cy="12" r="4"/>' + rays(8, 5.5, 9));
      case 2:  // Mine
        return S("#ffffff", '<circle cx="12" cy="12" r="4.5"/>' + rays(6, 4.5, 8.5));
      case 3:  // GigaMine
        return S("#ffffff", '<circle cx="12" cy="12" r="3"/><circle cx="12" cy="12" r="5.5"/>' + rays(6, 5.5, 9));
      case 4:  // Missile: dart with fins
        return S("#f2f2f2", '<path d="M12 3 L15 9 L15 16 L18 20 M12 3 L9 9 L9 16 L6 20 M9 16 L15 16"/>');
      case 5:  // Shield
        return S("#66e5ff", '<path d="M5 5 L19 5 L19 12 L12 20 L5 12 Z"/>');
      case 6:  // Nova: starburst
        return S("#ff9933", rays(8, 2, 9.5, 5.5));
      case 7:  // Beam: violet star (the pickup)
        return star("#b366ff");
      case 8:  // Lance: amber star (the pickup)
        return star("#ffd966");
      case 9:  // Shock: lightning
        return S("#99e5ff", '<path d="M15 3 L9 13 L14 14 L9 21"/>');
      case 10: // Turret: ring with barrel
        return S("#4de5cc", '<circle cx="12" cy="12" r="5"/><path d="M12 7 L12 2"/>');
    }
    return null;
  }
  function applyWeaponIcon(sel: string, kind: number): void {
    const el = document.querySelector<HTMLElement>(sel);
    if (!el) return;
    const svg = kind >= 0 ? weaponIconSvg(kind) : null;
    if (svg) {
      el.style.backgroundImage = `url("data:image/svg+xml,${encodeURIComponent(svg)}")`;
      el.style.backgroundRepeat = "no-repeat";
      el.style.backgroundPosition = "center";
      el.style.backgroundSize = "55% 55%";
    } else {
      el.style.backgroundImage = "";
    }
  }
  function setWeaponKinds(primary: number, secondary: number): void {
    applyWeaponIcon(".touch-shoot", primary);
    applyWeaponIcon(".touch-mine", secondary);
  }
  (window as any).setWeaponKinds = setWeaponKinds;

  // ---- Game-over promo banner ----------------------------------------
  // The web build deliberately has no netplay or leaderboard (LEADERBOARD.md:
  // NetBoard::create() is null on web) — this banner is where the free
  // version finally says so, at the moment the run ends. glgame.cpp's
  // game-over latches call window.newtGameOver(score, players); the banner
  // offers the site leaderboard with ?score= (the page shows where the run
  // WOULD have placed) and the Steam store, UTM-tagged so Steam's traffic
  // report attributes web-game referrals. Absolute URLs on both: the
  // itch.io deploy serves this same bundle off-domain.
  let _goBanner: HTMLElement | null = null;
  let _goRank: HTMLAnchorElement | null = null;

  function hideGameOverBanner(): void {
    if (_goBanner) _goBanner.style.display = "none";
  }

  function showGameOverBanner(score: number, playerCount: number): void {
    score = Math.max(0, Math.floor(Number(score) || 0));
    // The board keeps one co-op slot: every run with >= 2 players competes
    // on the players=2 board (FOURPLAYER.md D10).
    const boardPlayers = playerCount >= 2 ? 2 : 1;
    if (!_goBanner) {
      const el = document.createElement("div");
      el.id = "go-banner";
      const rank = document.createElement("a");
      rank.target = "_blank";
      rank.rel = "noopener";
      const steam = document.createElement("a");
      steam.className = "go-steam";
      steam.target = "_blank";
      steam.rel = "noopener";
      steam.href =
          "https://store.steampowered.com/app/4536720/Newtonia/" +
          "?utm_source=newtonia_webgame&utm_medium=referral&utm_campaign=gameover";
      steam.textContent = "ONLINE CO-OP + LEADERBOARDS — GET IT ON STEAM";
      const close = document.createElement("button");
      close.textContent = "✕";
      close.setAttribute("aria-label", "Dismiss");
      close.addEventListener("click", () => {
        hideGameOverBanner();
        canvas.focus(); // hand the keys back to the game
      });
      el.appendChild(rank);
      el.appendChild(steam);
      el.appendChild(close);
      document.getElementById("game-container")!.appendChild(el);
      _goBanner = el;
      _goRank = rank;
    }
    _goRank!.href =
        "https://newtonia.metonymous.com/leaderboard/" +
        `?score=${score}&players=${boardPlayers}` +
        "&utm_source=newtonia_webgame&utm_medium=referral&utm_campaign=gameover";
    _goRank!.textContent =
        `SCORE ${score.toLocaleString("en-US")} — SEE WHERE YOU'D RANK`;
    _goBanner.style.display = "flex";
  }
  (window as any).newtGameOver = showGameOverBanner;

  // Builds the touch UI and returns a resize handler.
  // The caller is responsible for adding/removing the resize listener.
  function buildTouchControls(): () => void {
    const container = document.getElementById("touch-controls")!;
    container.innerHTML = "";

    // ------------------------------------------------------------------
    // Left half — floating analog joystick
    // ------------------------------------------------------------------
    const joyZone = document.createElement("div");
    joyZone.className = "joy-zone";

    const joyBase = document.createElement("div");
    joyBase.className = "joy-base";
    joyBase.style.display = "none";

    const joyNub = document.createElement("div");
    joyNub.className = "joy-nub";
    joyNub.style.display = "none";

    container.appendChild(joyZone);
    container.appendChild(joyBase);
    container.appendChild(joyNub);

    let joyFinger: number | null = null;
    let joyCX = 0, joyCY = 0, joyRad = 0;

    // Radius is captured at touchstart and reused for the whole drag — avoids
    // a getBoundingClientRect() call on every touchmove.
    function showJoystick(x: number, y: number, rad: number): void {
      const baseSize = rad * 2, nubSize = rad * 0.62;
      joyBase.style.cssText = `display:block;width:${baseSize}px;height:${baseSize}px;left:${x}px;top:${y}px;opacity:1;`;
      joyNub.style.cssText  = `display:block;width:${nubSize}px;height:${nubSize}px;left:${x}px;top:${y}px;opacity:1;`;
      joyCX = x; joyCY = y; joyRad = rad;
    }

    function moveJoystick(x: number, y: number): void {
      const dx = x - joyCX, dy = y - joyCY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      const clamped = Math.min(dist, joyRad);
      const nx = dist > 0.5 ? (dx / dist) * (clamped / joyRad) : 0;
      const ny = dist > 0.5 ? (dy / dist) * (clamped / joyRad) : 0;
      joyNub.style.left = `${joyCX + nx * joyRad}px`;
      joyNub.style.top  = `${joyCY + ny * joyRad}px`;
      // C++ touch_joystick_input: ny < 0 = thrust, ny > 0 = reverse.
      // Screen dy is already negative when pushing up, so pass ny directly.
      callTouchJoystick(nx, ny);
    }

    function hideJoystick(): void {
      joyFinger = null;
      callTouchJoystick(0, 0);
      positionJoyPlaceholder();
    }

    // Show a faint placeholder at the default position so the user knows
    // where the joystick zone is before touching.
    function positionJoyPlaceholder(): void {
      if (_inMenuMode) return;
      const r = canvas.getBoundingClientRect();
      if (r.width === 0) return; // layout not ready yet
      const rad = Math.min(r.width, r.height) * 0.26;
      const baseSize = rad * 2, nubSize = rad * 0.62;
      const px = r.left + r.width * 0.18;
      const py = r.top  + r.height * 0.75;
      joyBase.style.cssText = `display:block;width:${baseSize}px;height:${baseSize}px;left:${px}px;top:${py}px;opacity:0.4;`;
      joyNub.style.cssText  = `display:block;width:${nubSize}px;height:${nubSize}px;left:${px}px;top:${py}px;opacity:0.4;`;
    }
    _joyPlaceholderEls = [joyBase, joyNub];
    _positionJoyPlaceholder = positionJoyPlaceholder;
    requestAnimationFrame(positionJoyPlaceholder);

    joyZone.addEventListener("touchstart", (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        const t = e.changedTouches[i];
        if (joyFinger === null) {
          joyFinger = t.identifier;
          const r = canvas.getBoundingClientRect();
          // clientX/Y are viewport-relative; touch-controls is position:fixed so no offset needed.
          showJoystick(t.clientX, t.clientY, Math.min(r.width, r.height) * 0.26);
          break;
        }
      }
    }, { passive: false });

    joyZone.addEventListener("touchmove", (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        const t = e.changedTouches[i];
        if (t.identifier === joyFinger) {
          moveJoystick(t.clientX, t.clientY);
          break;
        }
      }
    }, { passive: false });

    const onJoyEnd = (e: TouchEvent) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        if (e.changedTouches[i].identifier === joyFinger) { hideJoystick(); break; }
      }
    };
    joyZone.addEventListener("touchend",    onJoyEnd, { passive: false });
    joyZone.addEventListener("touchcancel", onJoyEnd, { passive: false });

    // ------------------------------------------------------------------
    // Right half — action buttons with visual press feedback
    // ------------------------------------------------------------------
    interface BtnCfg { label: string; key: string; cls: string }

    const BUTTONS: BtnCfg[] = [
      { label: "",  key: " ", cls: "touch-btn touch-shoot" },
      { label: "",  key: "x", cls: "touch-btn touch-mine"  },
      { label: "",  key: "e", cls: "touch-btn touch-boost" },
      // Pause: the top-right tap zone (web_main.cpp finger_down) existed
      // with NO visual — the comment there called it "the visible
      // top-right button" but nothing ever drew it on web (the native
      // OSD's pause circle only draws where touch_osd_enabled()). The
      // bars are CSS pseudo-elements; 'p' is the pause toggle.
      { label: "",  key: "p", cls: "touch-btn touch-pause" },
    ];

    BUTTONS.forEach(({ label, key, cls }) => {
      const btn = document.createElement("div");
      btn.className = cls;
      btn.textContent = label;

      const dispatchKey = (type: string) => {
        canvas.dispatchEvent(new KeyboardEvent(type, {
          key,
          code: key === " " ? "Space" : `Key${key.toUpperCase()}`,
          bubbles: true,
          cancelable: true,
        }));
      };

      // Track active fingers so multi-finger presses keep the button held.
      const activeFingers = new Set<number>();

      btn.addEventListener("touchstart", (e) => {
        e.preventDefault();
        for (let i = 0; i < e.changedTouches.length; i++) {
          const id = e.changedTouches[i].identifier;
          if (!activeFingers.has(id)) {
            if (activeFingers.size === 0) dispatchKey("keydown");
            activeFingers.add(id);
          }
        }
        btn.classList.add("pressed");
      }, { passive: false });

      const onBtnEnd = (e: TouchEvent) => {
        e.preventDefault();
        for (let i = 0; i < e.changedTouches.length; i++) {
          activeFingers.delete(e.changedTouches[i].identifier);
        }
        if (activeFingers.size === 0) {
          dispatchKey("keyup");
          btn.classList.remove("pressed");
        }
      };
      btn.addEventListener("touchend",    onBtnEnd, { passive: false });
      btn.addEventListener("touchcancel", onBtnEnd, { passive: false });

      container.appendChild(btn);
    });

    // Capture button elements once; reused by the resize handler to avoid
    // repeated querySelector calls.
    // Button centres sit clear of the canvas centre-pause zone (x <= 0.60
    // in web_main.cpp finger_down) — at 0.62 the shoot circle's left edge
    // was ~0.57, so a near-miss to its left hit the pause zone instead
    // (Glenn, 2026-07-17). Keep these in sync with touch_to_key's zones.
    const circleButtons = [
      { el: container.querySelector<HTMLElement>(".touch-shoot")!, cx: 0.70, cy: 0.75, d: 1.0 },
      { el: container.querySelector<HTMLElement>(".touch-mine")!,  cx: 0.90, cy: 0.75, d: 1.0 },
      // Boost: above and between the pair (the thumb triangle), matching
      // the native OSD layout in touch_controls.cpp.
      { el: container.querySelector<HTMLElement>(".touch-boost")!, cx: 0.80, cy: 0.63, d: 1.0 },
      // Pause: centred in the top-right tap zone (x >= 0.75, y < 0.25).
      { el: container.querySelector<HTMLElement>(".touch-pause")!, cx: 0.875, cy: 0.12, d: 0.62 },
    ];
    _circleButtonEls = circleButtons.map(b => b.el);

    // Full-screen overlay active during menu: any tap dispatches Enter to start the game.
    const menuOverlay = document.createElement("div");
    menuOverlay.className = "menu-overlay";
    menuOverlay.addEventListener("touchend", (e) => {
      e.preventDefault();
      const t = e.changedTouches[0];
      const r = canvas.getBoundingClientRect();
      const nx = t ? (t.clientX - r.left) / r.width  : 0.5;
      const ny = t ? (t.clientY - r.top)  / r.height : 0.5;
      (Module as ModuleEx)._web_menu_tap?.(nx, ny);
    }, { passive: false });
    container.appendChild(menuOverlay);
    _menuOverlay = menuOverlay;

    // Size and centre circular buttons. transform is handled by CSS so that
    // the .pressed scale animation works without fighting inline styles.
    function sizeCircleButtons(): void {
      const r = canvas.getBoundingClientRect();
      if (r.width === 0) return; // layout not ready yet
      const diam = Math.min(r.width, r.height) * 0.19;
      for (const { el, cx, cy, d } of circleButtons) {
        el.style.width  = `${diam * d}px`;
        el.style.height = `${diam * d}px`;
        el.style.left   = `${r.left + r.width * cx}px`;
        el.style.top    = `${r.top + r.height * cy}px`;
      }
      if (joyFinger === null) positionJoyPlaceholder();
    }

    // Use ResizeObserver so buttons are sized correctly on initial layout
    // (requestAnimationFrame fires too early, before the canvas has its final size).
    _resizeObserver?.disconnect();
    _resizeObserver = new ResizeObserver(sizeCircleButtons);
    _resizeObserver.observe(canvas);
    return sizeCircleButtons;
  }

  // Tracks the active resize listener so it can be removed on rebuild.
  let _resizeFn: (() => void) | null = null;
  let _resizeObserver: ResizeObserver | null = null;

  function applyTouchVisibility(): void {
    const tc = document.getElementById("touch-controls")!;
    if (_resizeFn) { window.removeEventListener("resize", _resizeFn); _resizeFn = null; }
    _resizeObserver?.disconnect(); _resizeObserver = null;
    if (TOUCH_MEDIA.matches) {
      tc.style.display = "block";
      _resizeFn = buildTouchControls();
      window.addEventListener("resize", _resizeFn);
      setMenuMode(true); // Start in menu: hide action buttons, show tap overlay
    } else {
      tc.style.display = "none";
    }
  }

  TOUCH_MEDIA.addEventListener("change", applyTouchVisibility);
  applyTouchVisibility();

  // ---- Focus canvas on first interaction so keyboard works ----
  document.addEventListener(
    "pointerdown",
    () => canvas.focus(),
    { once: false }
  );
})();
