#include "world_sound.h"

namespace {
  WorldSound::VolumeFn g_fn = NULL;
  const void *g_ctx = NULL;
}

namespace WorldSound {

void set_listener(VolumeFn fn, const void *ctx) {
  g_fn = fn;
  g_ctx = ctx;
}

void clear_listener(const void *ctx) {
  if (g_ctx != ctx) return;  // someone else already took over
  g_fn = NULL;
  g_ctx = NULL;
}

float volume_at(Point p) {
  if (g_fn == NULL) return 1.0f;
  float v = g_fn(g_ctx, p);
  if (v < 0.0f) return 0.0f;
  return v > 1.0f ? 1.0f : v;
}

void play(Mix_Chunk *chunk, Point at, float base) {
  if (chunk == NULL) return;
  float v = volume_at(at) * base;
  if (v <= 0.0f) return;
  // Always set the volume: these are shared static chunks, so whatever the
  // last play left behind is what the next one would inherit.
  Mix_VolumeChunk(chunk, (int)(MIX_MAX_VOLUME * (v > 1.0f ? 1.0f : v)));
  Mix_PlayChannel(-1, chunk, 0);
}

}  // namespace WorldSound
