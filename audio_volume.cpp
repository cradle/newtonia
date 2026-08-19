#include "audio_volume.h"

#include <SDL_mixer.h>

#include "preferences.h"

namespace AudioVolume {

void apply() {
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
  Mix_MasterVolume((int)(MIX_MAX_VOLUME * g_prefs.master_volume + 0.5f));
#endif
  Mix_VolumeMusic((int)(MIX_MAX_VOLUME * g_prefs.master_volume *
                            g_prefs.music_volume + 0.5f));
}

float music_scale() { return g_prefs.music_volume; }

}  // namespace AudioVolume
