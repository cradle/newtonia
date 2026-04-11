#pragma once

// User preferences persisted to an INI file in the SDL pref path.
// Each setting has a sensible default; missing keys in the file are silently
// ignored so old preference files remain valid after new settings are added.

struct Preferences {
    bool fullscreen     = true;   // desktop only; ignored on mobile/web
    bool rotate_view    = true;   // camera follows ship heading
    bool friendly_fire  = true;   // players damage each other
    int  window_width   = 800;    // last windowed resolution (desktop only)
    int  window_height  = 600;
};

// Populate g_prefs from disk.  Call once at startup (after the pref path is
// available, i.e. after IDBFS sync on web).  Returns defaults when no file
// exists or a key is absent.
void load_preferences();

// Write g_prefs to disk immediately.  On web, also flushes to IndexedDB.
void save_preferences();

// Global preferences instance.  Read/write it directly; call save_preferences()
// to persist changes.
extern Preferences g_prefs;
