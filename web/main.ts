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
    // <season>/<run_id>: season is the header's stamp (printable ASCII, no
    // slash/backslash, <= 23 chars — the worker re-validates), run_id
    // decimal. Anything else is ignored.
    if (!spec || !/^[!-~]{1,23}\/[0-9]{1,20}$/.test(spec) ||
        spec.indexOf("\\") !== -1) return;
    const host = q.get("board") === "beta"
        ? "https://newtonia-board-beta.gfmcc.workers.dev"
        : "https://newtonia-board.gfmcc.workers.dev";
    const [season, run] = spec.split("/");
    fetch(`${host}/replay/${encodeURIComponent(season)}/${run}.nrp`)
      .then((r) => {
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        return r.arrayBuffer();
      })
      .then((buf) => {
        const bytes = new Uint8Array(buf);
        const m = Module as any;
        let tries = 0;
        const deliver = () => {
          if (m._web_watch_replay && m._malloc && m._free && m.HEAPU8) {
            const ptr = m._malloc(bytes.length);
            m.HEAPU8.set(bytes, ptr);
            const res = m._web_watch_replay(ptr, bytes.length);
            m._free(ptr);
            if (res === 0 && tries++ < 600) {
              setTimeout(deliver, 100); // IDBFS still mounting (~60 s cap)
              return;
            }
            if (res < 0)
              alert("This replay can't be played by the current web build " +
                    "(it was recorded by a different game version).");
            return;
          }
          if (tries++ < 600) setTimeout(deliver, 100);
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

  // Called from C++ via EM_ASM when game state changes.
  function setMenuMode(isMenu: boolean): void {
    _inMenuMode = isMenu;
    for (const el of _circleButtonEls) {
      el.style.display = isMenu ? "none" : "";
    }
    for (const el of _joyPlaceholderEls) {
      el.style.display = isMenu ? "none" : "";
    }
    if (!isMenu) {
      _resizeFn?.(); // Ensure buttons are correctly sized when they become visible
      _positionJoyPlaceholder?.();
    }
    if (_menuOverlay) _menuOverlay.style.display = isMenu ? "block" : "none";
  }
  (window as any).setMenuMode = setMenuMode;

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
      { el: container.querySelector<HTMLElement>(".touch-shoot")!, cx: 0.70, cy: 0.75 },
      { el: container.querySelector<HTMLElement>(".touch-mine")!,  cx: 0.90, cy: 0.75 },
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
      for (const { el, cx, cy } of circleButtons) {
        el.style.width  = `${diam}px`;
        el.style.height = `${diam}px`;
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
