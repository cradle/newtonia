#include "audio_volume.h"

#include <SDL_mixer.h>

#include "preferences.h"

namespace AudioVolume {

// Headroom on the title stream: title.wav is hot enough that at the
// sliders' FULL it clipped audibly on a field machine (2026-09-09).
// A fixed fraction of the USER level, so the slider still means what it
// says and the gameplay cues under master alone are untouched.
static const float TITLE_SCALE = 0.75f;

void apply() {
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
  Mix_MasterVolume((int)(MIX_MAX_VOLUME * g_prefs.master_volume + 0.5f));
#endif
  Mix_VolumeMusic((int)(MIX_MAX_VOLUME * TITLE_SCALE * g_prefs.master_volume *
                            g_prefs.music_volume + 0.5f));
}

float music_scale() { return g_prefs.music_volume; }

}  // namespace AudioVolume
