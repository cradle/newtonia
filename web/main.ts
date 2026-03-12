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

  // ---- Fullscreen ----
  fsBtn.addEventListener("click", () => {
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
  });

  document.addEventListener("fullscreenchange", () => {
    fsBtn.title = document.fullscreenElement ? "Exit fullscreen" : "Fullscreen";
  });

  // ---- Mute ----
  // Emscripten SDL2_mixer exposes volume via the C API; we use the Web Audio
  // API as a simpler cross-cutting mute that doesn't require EXPORTED_FUNCTIONS.
  let muted = false;
  let audioCtx: AudioContext | null = null;
  let gainNode: GainNode | null = null;

  function getAudioContext(): AudioContext | null {
    // Emscripten creates a global AudioContext accessible via window.
    // If it isn't available yet, return null and retry on first user gesture.
    const w = window as Window & { SDL?: { audioContext?: AudioContext } };
    return w.SDL?.audioContext ?? null;
  }

  function ensureGain(): GainNode | null {
    if (gainNode) return gainNode;
    const ctx = getAudioContext();
    if (!ctx) return null;
    audioCtx = ctx;
    gainNode = ctx.createGain();
    gainNode.connect(ctx.destination);
    // Reconnect Emscripten's master output through our gain node.
    // SDL2/Emscripten uses ctx.destination as its output; intercept by
    // disconnecting and rerouting is fragile, so we rely on AudioContext.suspend
    // instead (simpler and reliable).
    return gainNode;
  }

  muteBtn.addEventListener("click", () => {
    muted = !muted;
    muteBtn.textContent = muted ? "🔇" : "🔊";
    muteBtn.title = muted ? "Unmute" : "Mute";

    const ctx = getAudioContext() ?? audioCtx;
    if (ctx) {
      if (muted) {
        ctx.suspend();
      } else {
        ctx.resume();
      }
    }
    ensureGain();
  });

  // ---- On-screen touch controls ----
  // Left half: floating analog joystick that calls Module._web_touch_joystick(nx, ny).
  // Right half: large circular action buttons with press-state visual feedback.
  // Multi-touch is supported — joystick and action buttons track independent fingers.

  const TOUCH_MEDIA = window.matchMedia("(pointer: coarse)");

  // Extend Module type to include our exported C function.
  type ModuleEx = typeof Module & { _web_touch_joystick?(nx: number, ny: number): void };

  function callTouchJoystick(nx: number, ny: number): void {
    (Module as ModuleEx)._web_touch_joystick?.(nx, ny);
  }

  // Module-level refs so setMenuMode can show/hide them.
  let _circleButtonEls: HTMLElement[] = [];
  let _menuOverlay: HTMLElement | null = null;

  // Called from C++ via EM_ASM when game state changes.
  function setMenuMode(isMenu: boolean): void {
    for (const el of _circleButtonEls) {
      el.style.display = isMenu ? "none" : "";
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
      joyBase.style.cssText = `display:block;width:${baseSize}px;height:${baseSize}px;left:${x}px;top:${y}px;`;
      joyNub.style.cssText  = `display:block;width:${nubSize}px;height:${nubSize}px;left:${x}px;top:${y}px;`;
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
      joyBase.style.display = "none";
      joyNub.style.display  = "none";
      callTouchJoystick(0, 0);
    }

    joyZone.addEventListener("touchstart", (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        const t = e.changedTouches[i];
        if (joyFinger === null) {
          joyFinger = t.identifier;
          const r = canvas.getBoundingClientRect();
          showJoystick(t.clientX - r.left, t.clientY - r.top, Math.min(r.width, r.height) * 0.13);
          break;
        }
      }
    }, { passive: false });

    joyZone.addEventListener("touchmove", (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        const t = e.changedTouches[i];
        if (t.identifier === joyFinger) {
          const r = canvas.getBoundingClientRect();
          moveJoystick(t.clientX - r.left, t.clientY - r.top);
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
    const circleButtons = [
      { el: container.querySelector<HTMLElement>(".touch-shoot")!, cx: 0.62, cy: 0.85 },
      { el: container.querySelector<HTMLElement>(".touch-mine")!,  cx: 0.85, cy: 0.85 },
    ];
    _circleButtonEls = circleButtons.map(b => b.el);

    // Full-screen overlay active during menu: any tap dispatches Enter to start the game.
    const menuOverlay = document.createElement("div");
    menuOverlay.className = "menu-overlay";
    menuOverlay.addEventListener("touchend", (e) => {
      e.preventDefault();
      canvas.dispatchEvent(new KeyboardEvent("keyup", {
        key: "Enter", code: "Enter", bubbles: true, cancelable: true,
      }));
    }, { passive: false });
    container.appendChild(menuOverlay);
    _menuOverlay = menuOverlay;

    // Size and centre circular buttons. transform is handled by CSS so that
    // the .pressed scale animation works without fighting inline styles.
    function sizeCircleButtons(): void {
      const r = canvas.getBoundingClientRect();
      const diam = Math.min(r.width, r.height) * 0.19;
      for (const { el, cx, cy } of circleButtons) {
        el.style.width  = `${diam}px`;
        el.style.height = `${diam}px`;
        el.style.left   = `${r.width  * cx}px`;
        el.style.top    = `${r.height * cy}px`;
      }
    }

    sizeCircleButtons();
    return sizeCircleButtons;
  }

  // Tracks the active resize listener so it can be removed on rebuild.
  let _resizeFn: (() => void) | null = null;

  function applyTouchVisibility(): void {
    const tc = document.getElementById("touch-controls")!;
    if (_resizeFn) { window.removeEventListener("resize", _resizeFn); _resizeFn = null; }
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
