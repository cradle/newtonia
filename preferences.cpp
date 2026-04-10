#include "preferences.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Preferences g_prefs;

static const char* PREF_ORG  = "cc.gfm";
static const char* PREF_APP  = "newtonia";
static const char* PREF_FILE = "preferences.ini";

static std::string pref_filepath() {
    char *path = SDL_GetPrefPath(PREF_ORG, PREF_APP);
    if (!path) return "";
    std::string fp = std::string(path) + PREF_FILE;
    SDL_free(path);
    return fp;
}

void load_preferences() {
    // Start from struct defaults.
    g_prefs = Preferences();

    std::string fp = pref_filepath();
    if (fp.empty()) return;

    FILE *f = fopen(fp.c_str(), "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline / carriage return.
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        char key[128], val[128];
        if (sscanf(line, "%127[^=]=%127s", key, val) != 2) continue;

        // Unknown keys are silently ignored so older files stay valid.
        if (strcmp(key, "fullscreen") == 0)
            g_prefs.fullscreen = (val[0] == '1');
        else if (strcmp(key, "rotate_view") == 0)
            g_prefs.rotate_view = (val[0] == '1');
    }
    fclose(f);
}

void save_preferences() {
    std::string fp = pref_filepath();
    if (fp.empty()) return;

    FILE *f = fopen(fp.c_str(), "w");
    if (!f) return;

    fprintf(f, "fullscreen=%d\n",  g_prefs.fullscreen  ? 1 : 0);
    fprintf(f, "rotate_view=%d\n", g_prefs.rotate_view ? 1 : 0);
    fclose(f);

#ifdef __EMSCRIPTEN__
    // Flush the in-memory filesystem to IndexedDB so preferences survive page
    // refreshes.
    EM_ASM(
        FS.syncfs(false, function(err) {
            if (err) console.error('[newtonia] IDBFS pref save failed:', err);
        });
    );
#endif
}
