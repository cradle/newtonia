#pragma once

// The user volume seam (the Options screen's AUDIO sub-menu writes
// Preferences::master_volume / music_volume; this pushes them onto the
// mixer). Two levels only, deliberately:
//
//  - MASTER rides SDL_mixer's channel master volume (2.6+), which scales
//    every CHANNEL — gunfire, world cues, the god-mode weapon music (a
//    gameplay cue with a warning phase, so it does not duck with the
//    music slider) — multiplied on top of whatever chunk/channel levels
//    the game's own attenuation sets, so nothing about WorldSound or the
//    per-ship scales changes. On a pre-2.6 mixer the macro guard leaves
//    channels unscaled (music still obeys) rather than failing the build.
//
//  - MUSIC scales the tunes relative to that: the title stream
//    (Mix_VolumeMusic carries master*music) and the intro/pause loops,
//    which live on CHANNELS, not the music stream — those sites multiply
//    their own chunk volume by music_scale() (master reaches them through
//    the channel master volume, so the fraction here excludes it).
//
// apply() is safe before Mix_OpenAudio — both levels are process-global
// state the mixer keeps — and is called from load_preferences() so every
// platform entry point inherits the stored levels, and from the AUDIO
// rows as they change so the menu music answers the adjustment audibly.
namespace AudioVolume {
  void apply();
  float music_scale();
}
