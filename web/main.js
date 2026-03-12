"use strict";
// UI chrome for the Newtonia web port.
// Handles fullscreen, mute, and on-screen touch controls.
// Compiled to main.js via tsc and loaded after the Emscripten script.
(function () {
    const canvas = document.getElementById("canvas");
    const fsBtn = document.getElementById("fullscreen-btn");
    const muteBtn = document.getElementById("mute-btn");
    // ---- Fullscreen ----
    fsBtn.addEventListener("click", () => {
        if (!document.fullscreenElement) {
            const container = document.getElementById("game-container");
            container.requestFullscreen().catch(() => {
                // Fallback: fullscreen just the canvas
                canvas.requestFullscreen?.();
            });
            fsBtn.textContent = "⛶";
        }
        else {
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
    let audioCtx = null;
    let gainNode = null;
    function getAudioContext() {
        // Emscripten creates a global AudioContext accessible via window.
        // If it isn't available yet, return null and retry on first user gesture.
        const w = window;
        return w.SDL?.audioContext ?? null;
    }
    function ensureGain() {
        if (gainNode)
            return gainNode;
        const ctx = getAudioContext();
        if (!ctx)
            return null;
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
            }
            else {
                ctx.resume();
            }
        }
        ensureGain();
    });
    // ---- On-screen touch controls (portrait / coarse pointer) ----
    // We build invisible tap zones over the canvas that map to the same
    // keyboard zones defined in web_main.cpp's touch_to_key().
    // Instead of fighting SDL2's touch event routing, we synthesise
    // SDL-style keyboard events by posting them into the canvas as
    // KeyboardEvent dispatches (Emscripten's SDL2 port listens on the canvas).
    const TOUCH_MEDIA = window.matchMedia("(pointer: coarse)");
    const ZONES = [
        // Left half — movement
        { label: "▲", key: "w", left: "5%", top: "5%", width: "40%", height: "35%" },
        { label: "◀", key: "a", left: "5%", top: "40%", width: "20%", height: "20%" },
        { label: "▶", key: "d", left: "25%", top: "40%", width: "20%", height: "20%" },
        { label: "▼", key: "s", left: "5%", top: "60%", width: "40%", height: "35%" },
        // Right half — actions
        { label: "↵", key: "Enter", left: "55%", top: "5%", width: "40%", height: "35%" },
        { label: "🔫", key: " ", left: "55%", top: "40%", width: "20%", height: "55%" },
        { label: "💣", key: "x", left: "75%", top: "40%", width: "20%", height: "55%" },
    ];
    function buildTouchControls() {
        const container = document.getElementById("touch-controls");
        container.innerHTML = "";
        ZONES.forEach((zone) => {
            const div = document.createElement("div");
            div.className = "touch-zone";
            div.style.cssText = [
                `left:${zone.left}`,
                `top:${zone.top}`,
                `width:${zone.width}`,
                `height:${zone.height}`,
                "display:flex",
                "align-items:center",
                "justify-content:center",
                "font-size:1.4rem",
                "color:rgba(255,255,255,0.18)",
                "border:1px solid rgba(255,255,255,0.08)",
                "border-radius:8px",
                "user-select:none",
                "-webkit-user-select:none",
            ].join(";");
            div.textContent = zone.label;
            const dispatch = (type) => {
                const actualKey = zone.key === " " ? " " : zone.key;
                canvas.dispatchEvent(new KeyboardEvent(type, {
                    key: actualKey,
                    code: actualKey === " " ? "Space" : `Key${actualKey.toUpperCase()}`,
                    bubbles: true,
                    cancelable: true,
                }));
            };
            div.addEventListener("touchstart", (e) => {
                e.preventDefault();
                dispatch("keydown");
            }, { passive: false });
            div.addEventListener("touchend", (e) => {
                e.preventDefault();
                dispatch("keyup");
            }, { passive: false });
            container.appendChild(div);
        });
    }
    function applyTouchVisibility() {
        const tc = document.getElementById("touch-controls");
        tc.style.display = TOUCH_MEDIA.matches ? "block" : "none";
        if (TOUCH_MEDIA.matches)
            buildTouchControls();
    }
    TOUCH_MEDIA.addEventListener("change", applyTouchVisibility);
    applyTouchVisibility();
    // ---- Focus canvas on first interaction so keyboard works ----
    document.addEventListener("pointerdown", () => canvas.focus(), { once: false });
})();
