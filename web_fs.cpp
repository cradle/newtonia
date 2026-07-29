#include "web_fs.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void web_fs_sync(const char *why) {
#ifdef __EMSCRIPTEN__
    // NOTE: no object literals or brace-level commas in here — EM_ASM is a
    // variadic macro, so a comma inside { } splits the JS into macro
    // arguments and the build fails with errors naming the object's fields
    // ("use of undeclared identifier 'again'"). Commas inside parentheses
    // are safe. Flat properties it is.
    EM_ASM({
        var tag = UTF8ToString($0);
        if (Module.__nwtnSyncRunning === undefined) {
            Module.__nwtnSyncRunning = false;
            Module.__nwtnSyncAgain = false;
        }
        Module.__nwtnSyncWhy = tag;
        if (Module.__nwtnSyncRunning) {
            Module.__nwtnSyncAgain = true;
            return;
        }
        var run = function() {
            Module.__nwtnSyncRunning = true;
            FS.syncfs(false, function(err) {
                Module.__nwtnSyncRunning = false;
                if (err)
                    console.error('[newtonia] IDBFS sync failed (' +
                                  Module.__nwtnSyncWhy + '):', err);
                // Anything that asked while we were busy is covered by one
                // more pass — its writes are already in MEMFS.
                if (Module.__nwtnSyncAgain) {
                    Module.__nwtnSyncAgain = false;
                    run();
                }
            });
        };
        run();
    }, why ? why : "");
#else
    (void)why;
#endif
}
