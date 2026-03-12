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
    try { (Module as ModuleEx)._web_touch_joystick?.(nx, ny); } catch (_) { /* not loaded yet */ }
  }

  function buildTouchControls(): void {
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
    let joyCX = 0, joyCY = 0;

    function joyRadius(): number {
      const r = canvas.getBoundingClientRect();
      return Math.min(r.width, r.height) * 0.13;
    }

    function relPos(touch: Touch): { x: number; y: number } {
      const r = canvas.getBoundingClientRect();
      return { x: touch.clientX - r.left, y: touch.clientY - r.top };
    }

    function showJoystick(x: number, y: number): void {
      const rad = joyRadius();
      const baseSize = rad * 2;
      const nubSize  = rad * 0.62;
      joyBase.style.cssText = `display:block;width:${baseSize}px;height:${baseSize}px;left:${x}px;top:${y}px;`;
      joyNub.style.cssText  = `display:block;width:${nubSize}px;height:${nubSize}px;left:${x}px;top:${y}px;`;
      joyCX = x;
      joyCY = y;
    }

    function moveJoystick(x: number, y: number): void {
      const rad = joyRadius();
      const dx = x - joyCX, dy = y - joyCY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      const clamped = Math.min(dist, rad);
      const nx = dist > 0.5 ? (dx / dist) * (clamped / rad) : 0;
      const ny = dist > 0.5 ? (dy / dist) * (clamped / rad) : 0;
      joyNub.style.left = `${joyCX + nx * rad}px`;
      joyNub.style.top  = `${joyCY + ny * rad}px`;
      // Game Y-axis: positive = up; screen Y: positive = down — invert ny.
      callTouchJoystick(nx, -ny);
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
          const { x, y } = relPos(t);
          showJoystick(x, y);
          moveJoystick(x, y);
          break;
        }
      }
    }, { passive: false });

    joyZone.addEventListener("touchmove", (e) => {
      e.preventDefault();
      for (let i = 0; i < e.changedTouches.length; i++) {
        const t = e.changedTouches[i];
        if (t.identifier === joyFinger) {
          const { x, y } = relPos(t);
          moveJoystick(x, y);
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
      { label: "↵",  key: "Enter", cls: "touch-btn touch-enter" },
      { label: "🔫", key: " ",     cls: "touch-btn touch-shoot" },
      { label: "💣", key: "x",     cls: "touch-btn touch-mine"  },
    ];

    // Size and centre circular buttons based on the container dimensions.
    // Called once on build and again if the window resizes.
    function sizeCircleButtons(): void {
      const r = canvas.getBoundingClientRect();
      const minDim = Math.min(r.width, r.height);
      const btnR   = minDim * 0.095; // radius in px
      const diam   = btnR * 2;
      // Shoot: centred at (62%, 73%) of container
      const shootEl = container.querySelector<HTMLElement>(".touch-shoot");
      const mineEl  = container.querySelector<HTMLElement>(".touch-mine");
      if (shootEl) {
        shootEl.style.width  = `${diam}px`;
        shootEl.style.height = `${diam}px`;
        shootEl.style.left   = `${r.width  * 0.62}px`;
        shootEl.style.top    = `${r.height * 0.73}px`;
        shootEl.style.transform = "translate(-50%,-50%)";
      }
      if (mineEl) {
        mineEl.style.width  = `${diam}px`;
        mineEl.style.height = `${diam}px`;
        mineEl.style.left   = `${r.width  * 0.85}px`;
        mineEl.style.top    = `${r.height * 0.73}px`;
        mineEl.style.transform = "translate(-50%,-50%)";
      }
    }

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

    // Size circular buttons now and whenever the window resizes.
    sizeCircleButtons();
    window.addEventListener("resize", sizeCircleButtons);
  }

  function applyTouchVisibility(): void {
    const tc = document.getElementById("touch-controls")!;
    tc.style.display = TOUCH_MEDIA.matches ? "block" : "none";
    if (TOUCH_MEDIA.matches) buildTouchControls();
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
