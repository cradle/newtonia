#ifndef SOUND_CACHE_H
#define SOUND_CACHE_H

#include <map>
#include <string>

#include <SDL_mixer.h>

#include "asset_path.h"

// Decode each WAV once per path, then hand out cheap per-instance
// Mix_QuickLoad_RAW wrappers around the shared sample buffer.
//
// Why wrappers and not one shared chunk: chunk volume is per-chunk state
// the mixer reads DURING playback, and the codebase deliberately runs
// per-instance chunks so one ship's Mix_VolumeChunk can't retune another
// ship's sound mid-play. A QuickLoad wrapper keeps that (own header, own
// volume) at the cost of a struct — and Mix_FreeChunk stays safe in the
// existing destructors (allocated=0: frees the header, not the samples).
//
// Why it exists: Ship's constructor ran 13 Mix_LoadWAV disk-decodes and
// every Weapon ctor a couple more — ~50 ms per Ship. Constructing ONE
// station enemy hitched a frame, and the client rebuilds weapons on
// every 10 Hz apply (task #71's churn), so the cost was continuous.
//
// The master chunks live for the process; ~a dozen short WAVs.
inline Mix_Chunk *load_wav_cached(const char *rel_path) {
  static std::map<std::string, Mix_Chunk *> cache;
  Mix_Chunk *master;
  std::map<std::string, Mix_Chunk *>::iterator it = cache.find(rel_path);
  if (it != cache.end()) {
    master = it->second;
  } else {
    master = Mix_LoadWAV(asset_path(rel_path).c_str());
    // Cache failures too: a missing file logged once by the caller
    // shouldn't be re-probed on every ship spawn.
    cache[rel_path] = master;
  }
  if (master == NULL) return NULL;
  return Mix_QuickLoad_RAW(master->abuf, master->alen);
}

#endif
