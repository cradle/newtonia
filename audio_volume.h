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
// apply() is called from load_preferences() (covers any later pref
// reload), from the Menu constructor (the one platform-neutral point
// that is certainly AFTER Mix_OpenAudio — the open RESETS the music
// volume to full, so an apply() that ran before it was silently wiped;
// measured on 2.8, and the field symptom was stored settings that
// showed in the menu but never sounded on launch), and from the AUDIO
// rows as they change so the menu music answers the adjustment audibly.
namespace AudioVolume {
  void apply();
  float music_scale();
}
