#include "audio_volume.h"

#include <SDL_mixer.h>

#include "preferences.h"

namespace AudioVolume {

// Global headroom: the game mixed hot enough that the menu music clipped
// audibly at the sliders' FULL on a field machine (2026-09-09), so EVERY
// level — channels and the music stream alike — sits at this fraction of
// the user's setting. A fixed scale under the sliders, so FULL still
// means the top of the user's range and saved prefs are untouched. It
// reaches the channels through the channel master volume (2.6+, like
// the master slider itself); the music stream carries it explicitly
// because Mix_MasterVolume does not scale that stream.
static const float HEADROOM = 0.75f;

void apply() {
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
  Mix_MasterVolume((int)(MIX_MAX_VOLUME * HEADROOM * g_prefs.master_volume + 0.5f));
#endif
  Mix_VolumeMusic((int)(MIX_MAX_VOLUME * HEADROOM * g_prefs.master_volume *
                            g_prefs.music_volume + 0.5f));
}

float music_scale() { return g_prefs.music_volume; }

}  // namespace AudioVolume
