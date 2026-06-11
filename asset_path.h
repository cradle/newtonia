#ifndef ASSET_PATH_H
#define ASSET_PATH_H

#include <string>

// Resolves a path to a bundled asset (e.g. "audio/shoot.wav").
//
// On GDK (Xbox / Desktop) the working directory of a packaged title is not
// guaranteed to be the install root, so paths are prefixed with
// SDL_GetBasePath().  Everywhere else the relative path is returned
// unchanged: desktop runs from the app directory, Android routes file access
// through the asset manager, and Emscripten preloads audio/ into MEMFS at /.

#if defined(_GAMING_XBOX) || defined(_GAMING_DESKTOP)

#include <SDL.h>

inline std::string asset_path(const char *relative) {
  static std::string base;
  if (base.empty()) {
    char *p = SDL_GetBasePath();
    if (p) {
      base = p;
      SDL_free(p);
    }
  }
  return base + relative;
}

#else

inline std::string asset_path(const char *relative) {
  return std::string(relative);
}

#endif

#endif // ASSET_PATH_H
