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
  // Per-CHANNEL volume on a reserved pool, not per-chunk volume. These are
  // shared static chunks, and SDL_mixer applies chunk volume at MIX time to
  // every channel still playing the chunk — so leveling the chunk here
  // retro-leveled instances already ringing (a nearby explosion audibly
  // ducked to a far one's 0.2 mid-sample, or a quiet far ring jumped to
  // full). Each play instead gets its own channel level, fixed for its
  // lifetime. The pool is carved out of the platforms'
  // Mix_ReserveChannels(2 + POOL) call, so no -1 allocation — no
  // boost/shield/music loop — ever lands on a channel whose volume this
  // sets, and nothing else plays these chunks (the repo rule: every world
  // cue goes through here).
  Mix_VolumeChunk(chunk, MIX_MAX_VOLUME);  // the level rides the channel
  static int next = 0;
  int ch = -1;
  for (int i = 0; i < POOL; i++) {
    int c = FIRST_CHANNEL + (next + i) % POOL;
    if (!Mix_Playing(c)) { ch = c; break; }
  }
  // All busy: steal round-robin — the same fate a full mixer dealt these
  // cues on -1 (a dropped play), except the newest cue wins.
  if (ch == -1) ch = FIRST_CHANNEL + next;
  next = (ch - FIRST_CHANNEL + 1) % POOL;
  Mix_Volume(ch, (int)(MIX_MAX_VOLUME * (v > 1.0f ? 1.0f : v)));
  Mix_PlayChannel(ch, chunk, 0);
}

}  // namespace WorldSound
