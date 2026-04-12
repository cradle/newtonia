#include "preferences.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Preferences g_prefs;

Preferences::Preferences() {
    // p1_keys and general_keys use their struct default member initializers.
    // Override p2_keys with player-2 defaults.
    p2_keys.left           = 'j';
    p2_keys.right          = 'l';
    p2_keys.thrust         = 'i';
    p2_keys.shoot          = '/';
    p2_keys.reverse        = 'k';
    p2_keys.mine           = ',';
    p2_keys.next_weapon    = 'u';
    p2_keys.next_secondary = '.';
    p2_keys.boost          = 'o';
    p2_keys.teleport       = 'y';
    p2_keys.help           = 136; // F8  (128 + GLUT_KEY_F8)
}

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

// Parse a single key=value line from the INI file.
// val is a decimal integer for keybind entries (key code 0–255).
static void parse_line(const char *key, const char *val) {
    // Scalar preferences
    if (strcmp(key, "fullscreen") == 0) {
        g_prefs.fullscreen = (val[0] == '1');
    } else if (strcmp(key, "rotate_view") == 0) {
        g_prefs.rotate_view = (val[0] == '1');
    } else if (strcmp(key, "friendly_fire") == 0) {
        g_prefs.friendly_fire = (val[0] == '1');
    } else if (strcmp(key, "window_width") == 0) {
        int w = atoi(val);
        if (w > 0) g_prefs.window_width = w;
    } else if (strcmp(key, "window_height") == 0) {
        int h = atoi(val);
        if (h > 0) g_prefs.window_height = h;

    // Player 1 keybinds
    } else if (strcmp(key, "p1_left")           == 0) { g_prefs.p1_keys.left           = atoi(val);
    } else if (strcmp(key, "p1_right")          == 0) { g_prefs.p1_keys.right          = atoi(val);
    } else if (strcmp(key, "p1_thrust")         == 0) { g_prefs.p1_keys.thrust         = atoi(val);
    } else if (strcmp(key, "p1_shoot")          == 0) { g_prefs.p1_keys.shoot          = atoi(val);
    } else if (strcmp(key, "p1_reverse")        == 0) { g_prefs.p1_keys.reverse        = atoi(val);
    } else if (strcmp(key, "p1_mine")           == 0) { g_prefs.p1_keys.mine           = atoi(val);
    } else if (strcmp(key, "p1_next_weapon")    == 0) { g_prefs.p1_keys.next_weapon    = atoi(val);
    } else if (strcmp(key, "p1_next_secondary") == 0) { g_prefs.p1_keys.next_secondary = atoi(val);
    } else if (strcmp(key, "p1_boost")          == 0) { g_prefs.p1_keys.boost          = atoi(val);
    } else if (strcmp(key, "p1_teleport")       == 0) { g_prefs.p1_keys.teleport       = atoi(val);
    } else if (strcmp(key, "p1_help")           == 0) { g_prefs.p1_keys.help           = atoi(val);

    // Player 2 keybinds
    } else if (strcmp(key, "p2_left")           == 0) { g_prefs.p2_keys.left           = atoi(val);
    } else if (strcmp(key, "p2_right")          == 0) { g_prefs.p2_keys.right          = atoi(val);
    } else if (strcmp(key, "p2_thrust")         == 0) { g_prefs.p2_keys.thrust         = atoi(val);
    } else if (strcmp(key, "p2_shoot")          == 0) { g_prefs.p2_keys.shoot          = atoi(val);
    } else if (strcmp(key, "p2_reverse")        == 0) { g_prefs.p2_keys.reverse        = atoi(val);
    } else if (strcmp(key, "p2_mine")           == 0) { g_prefs.p2_keys.mine           = atoi(val);
    } else if (strcmp(key, "p2_next_weapon")    == 0) { g_prefs.p2_keys.next_weapon    = atoi(val);
    } else if (strcmp(key, "p2_next_secondary") == 0) { g_prefs.p2_keys.next_secondary = atoi(val);
    } else if (strcmp(key, "p2_boost")          == 0) { g_prefs.p2_keys.boost          = atoi(val);
    } else if (strcmp(key, "p2_teleport")       == 0) { g_prefs.p2_keys.teleport       = atoi(val);
    } else if (strcmp(key, "p2_help")           == 0) { g_prefs.p2_keys.help           = atoi(val);

    // General keybinds
    } else if (strcmp(key, "general_pause")                == 0) { g_prefs.general_keys.pause                = atoi(val);
    } else if (strcmp(key, "general_menu")                 == 0) { g_prefs.general_keys.menu                 = atoi(val);
    } else if (strcmp(key, "general_add_player2")          == 0) { g_prefs.general_keys.add_player2          = atoi(val);
    } else if (strcmp(key, "general_toggle_friendly_fire") == 0) { g_prefs.general_keys.toggle_friendly_fire = atoi(val);
    } else if (strcmp(key, "general_skip_level")           == 0) { g_prefs.general_keys.skip_level           = atoi(val);
    } else if (strcmp(key, "general_toggle_debug_grid")    == 0) { g_prefs.general_keys.toggle_debug_grid    = atoi(val);
    } else if (strcmp(key, "general_time_speed_up")        == 0) { g_prefs.general_keys.time_speed_up        = atoi(val);
    } else if (strcmp(key, "general_time_slow_down")       == 0) { g_prefs.general_keys.time_slow_down       = atoi(val);
    } else if (strcmp(key, "general_time_reset")           == 0) { g_prefs.general_keys.time_reset           = atoi(val);
    } else if (strcmp(key, "general_toggle_fullscreen")    == 0) { g_prefs.general_keys.toggle_fullscreen    = atoi(val);
    } else if (strcmp(key, "general_toggle_rotate_view")   == 0) { g_prefs.general_keys.toggle_rotate_view   = atoi(val);
    } else if (strcmp(key, "general_disable_behaviours")   == 0) { g_prefs.general_keys.disable_behaviours   = atoi(val);
    }
    // Unknown keys are silently ignored so older files stay valid.
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
        parse_line(key, val);
    }
    fclose(f);
}

void save_preferences() {
    std::string fp = pref_filepath();
    if (fp.empty()) return;

    FILE *f = fopen(fp.c_str(), "w");
    if (!f) return;

    // Scalar preferences
    fprintf(f, "fullscreen=%d\n",     g_prefs.fullscreen    ? 1 : 0);
    fprintf(f, "rotate_view=%d\n",    g_prefs.rotate_view   ? 1 : 0);
    fprintf(f, "friendly_fire=%d\n",  g_prefs.friendly_fire ? 1 : 0);
    fprintf(f, "window_width=%d\n",   g_prefs.window_width);
    fprintf(f, "window_height=%d\n",  g_prefs.window_height);

    // Player 1 keybinds
    fprintf(f, "p1_left=%d\n",           g_prefs.p1_keys.left);
    fprintf(f, "p1_right=%d\n",          g_prefs.p1_keys.right);
    fprintf(f, "p1_thrust=%d\n",         g_prefs.p1_keys.thrust);
    fprintf(f, "p1_shoot=%d\n",          g_prefs.p1_keys.shoot);
    fprintf(f, "p1_reverse=%d\n",        g_prefs.p1_keys.reverse);
    fprintf(f, "p1_mine=%d\n",           g_prefs.p1_keys.mine);
    fprintf(f, "p1_next_weapon=%d\n",    g_prefs.p1_keys.next_weapon);
    fprintf(f, "p1_next_secondary=%d\n", g_prefs.p1_keys.next_secondary);
    fprintf(f, "p1_boost=%d\n",          g_prefs.p1_keys.boost);
    fprintf(f, "p1_teleport=%d\n",       g_prefs.p1_keys.teleport);
    fprintf(f, "p1_help=%d\n",           g_prefs.p1_keys.help);

    // Player 2 keybinds
    fprintf(f, "p2_left=%d\n",           g_prefs.p2_keys.left);
    fprintf(f, "p2_right=%d\n",          g_prefs.p2_keys.right);
    fprintf(f, "p2_thrust=%d\n",         g_prefs.p2_keys.thrust);
    fprintf(f, "p2_shoot=%d\n",          g_prefs.p2_keys.shoot);
    fprintf(f, "p2_reverse=%d\n",        g_prefs.p2_keys.reverse);
    fprintf(f, "p2_mine=%d\n",           g_prefs.p2_keys.mine);
    fprintf(f, "p2_next_weapon=%d\n",    g_prefs.p2_keys.next_weapon);
    fprintf(f, "p2_next_secondary=%d\n", g_prefs.p2_keys.next_secondary);
    fprintf(f, "p2_boost=%d\n",          g_prefs.p2_keys.boost);
    fprintf(f, "p2_teleport=%d\n",       g_prefs.p2_keys.teleport);
    fprintf(f, "p2_help=%d\n",           g_prefs.p2_keys.help);

    // General keybinds
    fprintf(f, "general_pause=%d\n",                g_prefs.general_keys.pause);
    fprintf(f, "general_menu=%d\n",                 g_prefs.general_keys.menu);
    fprintf(f, "general_add_player2=%d\n",          g_prefs.general_keys.add_player2);
    fprintf(f, "general_toggle_friendly_fire=%d\n", g_prefs.general_keys.toggle_friendly_fire);
    fprintf(f, "general_skip_level=%d\n",           g_prefs.general_keys.skip_level);
    fprintf(f, "general_toggle_debug_grid=%d\n",    g_prefs.general_keys.toggle_debug_grid);
    fprintf(f, "general_time_speed_up=%d\n",        g_prefs.general_keys.time_speed_up);
    fprintf(f, "general_time_slow_down=%d\n",       g_prefs.general_keys.time_slow_down);
    fprintf(f, "general_time_reset=%d\n",           g_prefs.general_keys.time_reset);
    fprintf(f, "general_toggle_fullscreen=%d\n",    g_prefs.general_keys.toggle_fullscreen);
    fprintf(f, "general_toggle_rotate_view=%d\n",   g_prefs.general_keys.toggle_rotate_view);
    fprintf(f, "general_disable_behaviours=%d\n",   g_prefs.general_keys.disable_behaviours);

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
