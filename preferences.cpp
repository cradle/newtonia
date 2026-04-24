#include "preferences.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Preferences g_prefs;

static const float STAR_DENSITY_SCALES[] = {0.1f, 0.2f, 0.35f, 0.55f, 0.75f, 1.0f};

float star_density_scale() {
    int idx = g_prefs.star_density_index;
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    return STAR_DENSITY_SCALES[idx];
}

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
    p2_keys.help               = 136; // F8  (128 + GLUT_KEY_F8)
    p2_keys.toggle_rotate_view = ';'; // right of L, within IJKL cluster
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

// Serialise a key code to a human-readable INI value:
//   printable ASCII  → the character itself (e.g. "a", "/", "=")
//   space (32)       → "space"
//   escape (27)      → "escape"
//   return (13)      → "return"
//   tab (9)          → "tab"
//   F1–F12 (129–140) → "F1"–"F12"
//   anything else    → decimal integer (fallback)
static std::string key_to_ini(int key) {
    switch (key) {
        case ' ':  return "space";
        case 27:   return "escape";
        case 13:   return "return";
        case 9:    return "tab";
    }
    if (key >= 129 && key <= 140) {
        char buf[8];
        snprintf(buf, sizeof(buf), "F%d", key - 128);
        return buf;
    }
    if (key >= 33 && key < 127)
        return std::string(1, (char)key);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", key);
    return buf;
}

// Parse an INI value back to a key code.  Case-insensitive for named keys.
// Also accepts bare decimal integers so old files remain valid.
static int ini_to_key(const char *val) {
    if (!val || !val[0]) return 0;

    if (SDL_strcasecmp(val, "space")  == 0) return ' ';
    if (SDL_strcasecmp(val, "escape") == 0) return 27;
    if (SDL_strcasecmp(val, "esc")    == 0) return 27;
    if (SDL_strcasecmp(val, "return") == 0) return 13;
    if (SDL_strcasecmp(val, "enter")  == 0) return 13;
    if (SDL_strcasecmp(val, "tab")    == 0) return 9;

    // F1–F12 (case-insensitive prefix 'f' or 'F')
    if ((val[0] == 'F' || val[0] == 'f') && val[1] != '\0') {
        int n = atoi(val + 1);
        if (n >= 1 && n <= 12) return 128 + n;
    }

    // Single printable character
    if (val[1] == '\0' && (unsigned char)val[0] >= 33 && (unsigned char)val[0] < 127)
        return (unsigned char)val[0];

    return 0;
}

// Parse a single key=value line from the INI file.
static void parse_line(const char *key, const char *val) {
    // Scalar preferences
    if (strcmp(key, "fullscreen") == 0) {
        g_prefs.fullscreen = (val[0] == '1');
    } else if (strcmp(key, "rotate_view") == 0) {
        g_prefs.rotate_view = (val[0] == '1');
    } else if (strcmp(key, "friendly_fire") == 0) {
        g_prefs.friendly_fire = (val[0] == '1');
    } else if (strcmp(key, "star_density_index") == 0) {
        int v = atoi(val);
        if (v >= 0 && v <= 5) g_prefs.star_density_index = v;
    } else if (strcmp(key, "window_width") == 0) {
        int w = atoi(val);
        if (w > 0) g_prefs.window_width = w;
    } else if (strcmp(key, "window_height") == 0) {
        int h = atoi(val);
        if (h > 0) g_prefs.window_height = h;
    // Player 1 keybinds
    } else if (strcmp(key, "p1_left")           == 0) { g_prefs.p1_keys.left           = ini_to_key(val);
    } else if (strcmp(key, "p1_right")          == 0) { g_prefs.p1_keys.right          = ini_to_key(val);
    } else if (strcmp(key, "p1_thrust")         == 0) { g_prefs.p1_keys.thrust         = ini_to_key(val);
    } else if (strcmp(key, "p1_shoot")          == 0) { g_prefs.p1_keys.shoot          = ini_to_key(val);
    } else if (strcmp(key, "p1_reverse")        == 0) { g_prefs.p1_keys.reverse        = ini_to_key(val);
    } else if (strcmp(key, "p1_mine")           == 0) { g_prefs.p1_keys.mine           = ini_to_key(val);
    } else if (strcmp(key, "p1_next_weapon")    == 0) { g_prefs.p1_keys.next_weapon    = ini_to_key(val);
    } else if (strcmp(key, "p1_next_secondary") == 0) { g_prefs.p1_keys.next_secondary = ini_to_key(val);
    } else if (strcmp(key, "p1_boost")          == 0) { g_prefs.p1_keys.boost          = ini_to_key(val);
    } else if (strcmp(key, "p1_teleport")       == 0) { g_prefs.p1_keys.teleport       = ini_to_key(val);
    } else if (strcmp(key, "p1_help")           == 0) { g_prefs.p1_keys.help           = ini_to_key(val);
    } else if (strcmp(key, "p1_toggle_rotate_view")  == 0) { g_prefs.p1_keys.toggle_rotate_view = ini_to_key(val);
    } else if (strcmp(key, "p1_keyboard_sensitivity") == 0) {
        float v = (float)atof(val);
        if (v >= 0.1f && v <= 5.0f) g_prefs.p1_keys.keyboard_sensitivity = v;
    } else if (strcmp(key, "p1_camera_smoothing") == 0) {
        float v = (float)atof(val);
        if (v >= 0.0f && v <= 0.1f) g_prefs.p1_keys.camera_smoothing = v;

    // Player 2 keybinds
    } else if (strcmp(key, "p2_left")           == 0) { g_prefs.p2_keys.left           = ini_to_key(val);
    } else if (strcmp(key, "p2_right")          == 0) { g_prefs.p2_keys.right          = ini_to_key(val);
    } else if (strcmp(key, "p2_thrust")         == 0) { g_prefs.p2_keys.thrust         = ini_to_key(val);
    } else if (strcmp(key, "p2_shoot")          == 0) { g_prefs.p2_keys.shoot          = ini_to_key(val);
    } else if (strcmp(key, "p2_reverse")        == 0) { g_prefs.p2_keys.reverse        = ini_to_key(val);
    } else if (strcmp(key, "p2_mine")           == 0) { g_prefs.p2_keys.mine           = ini_to_key(val);
    } else if (strcmp(key, "p2_next_weapon")    == 0) { g_prefs.p2_keys.next_weapon    = ini_to_key(val);
    } else if (strcmp(key, "p2_next_secondary") == 0) { g_prefs.p2_keys.next_secondary = ini_to_key(val);
    } else if (strcmp(key, "p2_boost")          == 0) { g_prefs.p2_keys.boost          = ini_to_key(val);
    } else if (strcmp(key, "p2_teleport")       == 0) { g_prefs.p2_keys.teleport       = ini_to_key(val);
    } else if (strcmp(key, "p2_help")           == 0) { g_prefs.p2_keys.help           = ini_to_key(val);
    } else if (strcmp(key, "p2_toggle_rotate_view")  == 0) { g_prefs.p2_keys.toggle_rotate_view = ini_to_key(val);
    } else if (strcmp(key, "p2_keyboard_sensitivity") == 0) {
        float v = (float)atof(val);
        if (v >= 0.1f && v <= 5.0f) g_prefs.p2_keys.keyboard_sensitivity = v;
    } else if (strcmp(key, "p2_camera_smoothing") == 0) {
        float v = (float)atof(val);
        if (v >= 0.0f && v <= 0.1f) g_prefs.p2_keys.camera_smoothing = v;

    // General keybinds
    } else if (strcmp(key, "general_pause")                == 0) { g_prefs.general_keys.pause                = ini_to_key(val);
    } else if (strcmp(key, "general_menu")                 == 0) { g_prefs.general_keys.menu                 = ini_to_key(val);
    } else if (strcmp(key, "general_add_player2")          == 0) { g_prefs.general_keys.add_player2          = ini_to_key(val);
    } else if (strcmp(key, "general_toggle_friendly_fire") == 0) { g_prefs.general_keys.toggle_friendly_fire = ini_to_key(val);
    } else if (strcmp(key, "general_skip_level")           == 0) { g_prefs.general_keys.skip_level           = ini_to_key(val);
    } else if (strcmp(key, "general_toggle_debug_grid")    == 0) { g_prefs.general_keys.toggle_debug_grid    = ini_to_key(val);
    } else if (strcmp(key, "general_time_speed_up")        == 0) { g_prefs.general_keys.time_speed_up        = ini_to_key(val);
    } else if (strcmp(key, "general_time_slow_down")       == 0) { g_prefs.general_keys.time_slow_down       = ini_to_key(val);
    } else if (strcmp(key, "general_time_reset")           == 0) { g_prefs.general_keys.time_reset           = ini_to_key(val);
    } else if (strcmp(key, "general_toggle_fullscreen")    == 0) { g_prefs.general_keys.toggle_fullscreen    = ini_to_key(val);
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
    fprintf(f, "fullscreen=%d\n",              g_prefs.fullscreen         ? 1 : 0);
    fprintf(f, "rotate_view=%d\n",             g_prefs.rotate_view        ? 1 : 0);
    fprintf(f, "friendly_fire=%d\n",           g_prefs.friendly_fire      ? 1 : 0);
    fprintf(f, "star_density_index=%d\n",      g_prefs.star_density_index);
    fprintf(f, "window_width=%d\n",            g_prefs.window_width);
    fprintf(f, "window_height=%d\n",           g_prefs.window_height);

#define WRITE_KEY(name, val) fprintf(f, name "=%s\n", key_to_ini(val).c_str())

    // Player 1 keybinds
    WRITE_KEY("p1_left",           g_prefs.p1_keys.left);
    WRITE_KEY("p1_right",          g_prefs.p1_keys.right);
    WRITE_KEY("p1_thrust",         g_prefs.p1_keys.thrust);
    WRITE_KEY("p1_shoot",          g_prefs.p1_keys.shoot);
    WRITE_KEY("p1_reverse",        g_prefs.p1_keys.reverse);
    WRITE_KEY("p1_mine",           g_prefs.p1_keys.mine);
    WRITE_KEY("p1_next_weapon",    g_prefs.p1_keys.next_weapon);
    WRITE_KEY("p1_next_secondary", g_prefs.p1_keys.next_secondary);
    WRITE_KEY("p1_boost",          g_prefs.p1_keys.boost);
    WRITE_KEY("p1_teleport",       g_prefs.p1_keys.teleport);
    WRITE_KEY("p1_help",               g_prefs.p1_keys.help);
    WRITE_KEY("p1_toggle_rotate_view", g_prefs.p1_keys.toggle_rotate_view);
    fprintf(f, "p1_keyboard_sensitivity=%.2f\n", g_prefs.p1_keys.keyboard_sensitivity);
    fprintf(f, "p1_camera_smoothing=%.4f\n",     g_prefs.p1_keys.camera_smoothing);

    // Player 2 keybinds
    WRITE_KEY("p2_left",           g_prefs.p2_keys.left);
    WRITE_KEY("p2_right",          g_prefs.p2_keys.right);
    WRITE_KEY("p2_thrust",         g_prefs.p2_keys.thrust);
    WRITE_KEY("p2_shoot",          g_prefs.p2_keys.shoot);
    WRITE_KEY("p2_reverse",        g_prefs.p2_keys.reverse);
    WRITE_KEY("p2_mine",           g_prefs.p2_keys.mine);
    WRITE_KEY("p2_next_weapon",    g_prefs.p2_keys.next_weapon);
    WRITE_KEY("p2_next_secondary", g_prefs.p2_keys.next_secondary);
    WRITE_KEY("p2_boost",          g_prefs.p2_keys.boost);
    WRITE_KEY("p2_teleport",       g_prefs.p2_keys.teleport);
    WRITE_KEY("p2_help",               g_prefs.p2_keys.help);
    WRITE_KEY("p2_toggle_rotate_view", g_prefs.p2_keys.toggle_rotate_view);
    fprintf(f, "p2_keyboard_sensitivity=%.2f\n", g_prefs.p2_keys.keyboard_sensitivity);
    fprintf(f, "p2_camera_smoothing=%.4f\n",     g_prefs.p2_keys.camera_smoothing);

    // General keybinds
    WRITE_KEY("general_pause",                g_prefs.general_keys.pause);
    WRITE_KEY("general_menu",                 g_prefs.general_keys.menu);
    WRITE_KEY("general_add_player2",          g_prefs.general_keys.add_player2);
    WRITE_KEY("general_toggle_friendly_fire", g_prefs.general_keys.toggle_friendly_fire);
    WRITE_KEY("general_skip_level",           g_prefs.general_keys.skip_level);
    WRITE_KEY("general_toggle_debug_grid",    g_prefs.general_keys.toggle_debug_grid);
    WRITE_KEY("general_time_speed_up",        g_prefs.general_keys.time_speed_up);
    WRITE_KEY("general_time_slow_down",       g_prefs.general_keys.time_slow_down);
    WRITE_KEY("general_time_reset",           g_prefs.general_keys.time_reset);
    WRITE_KEY("general_toggle_fullscreen",    g_prefs.general_keys.toggle_fullscreen);

#undef WRITE_KEY

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
