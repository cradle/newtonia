#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Call only after the game-side analytics gate has accepted this action.
// No GA calls here; the collector only updates its bounded local counters.
inline void web_record_touch_control(int mask, bool touch = true) {
    if (mask <= 0) return;
    EM_ASM({
        try {
            if (window.newtoniaRecordTouchControl)
                window.newtoniaRecordTouchControl($0, !!$1);
        } catch (e) { /* Analytics must never interrupt gameplay. */ }
    }, mask, touch);
}
#endif
