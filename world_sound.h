#ifndef WORLD_SOUND_H
#define WORLD_SOUND_H

#include <SDL_mixer.h>

#include "point.h"

// Distance attenuation for sounds that belong to a place in the world
// rather than to a ship.
//
// Ships already carry their own listener distance (Ship::sound_volume_scale,
// set per tick by GLGame) and every gun play site multiplies by it — which is
// why a co-op partner's shooting fades with range. The impact cues did not:
// Asteroid's thud/ting/explode chunks are process-wide statics played from
// Asteroid::kill(), from Ship::collide_grid and from the net client's
// cosmetic-impact pass, none of which can see a ship's scale. They played at
// whatever volume the chunk happened to be left at — usually full — so the
// other player's bullets banging off rocks on the far side of the world
// sounded like they were hitting your own hull.
//
// GLGame installs itself as the listener for the life of a game and answers
// with the same rule its own cues use: online, distance to whoever the local
// camera follows; offline, distance to the NEAREST local player (in
// split-screen both players are sitting at the same speakers, so a sound
// either of them can see is a sound both of them should hear).
//
// Anything running without a game — the menu, tests — gets 1.0 and behaves
// exactly as before.
namespace WorldSound {
  typedef float (*VolumeFn)(const void *ctx, Point p);

  // World cues play on their own reserved channel pool with per-CHANNEL
  // volume, so a new play cannot retro-level instances of the same shared
  // chunk still ringing (chunk volume applies at mix time to every channel
  // playing it). Channels 0/1 stay the priority pair (glgame's
  // play_priority_chunk); the pool is 2..2+POOL-1. Every platform entry
  // point must reserve the whole span: Mix_ReserveChannels(FIRST_CHANNEL +
  // POOL).
  const int FIRST_CHANNEL = 2;
  const int POOL = 8;

  // Install/remove the listener. clear() ignores a ctx that is not the
  // current one, so a departing state cannot mute the state that replaced
  // it (states are constructed before their predecessor is deleted).
  void set_listener(VolumeFn fn, const void *ctx);
  void clear_listener(const void *ctx);

  // 0 = out of earshot, 1 = right on top of the listener.
  float volume_at(Point p);

  // Play chunk as if it happened at `at`. `base` scales the full-volume
  // level for cues that are deliberately quieter than the rest.
  // Out of earshot it plays nothing and takes no mixer channel.
  void play(Mix_Chunk *chunk, Point at, float base = 1.0f);
}

#endif
