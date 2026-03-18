package org.newtonia;

// NewtoniaActivity extends SDL's SDLActivity.
// SDLActivity sets up the EGL surface, event loop bridge, and calls
// SDL_main() in the native library.

import android.content.Context;
import android.media.AudioManager;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

public class NewtoniaActivity extends SDLActivity {

    // Device's native audio output parameters, populated in onCreate() from
    // AudioManager so the native layer can open SDL2_mixer at the optimal
    // sample rate and buffer size for this hardware.
    static int sOptimalSampleRate      = 48000;
    static int sOptimalFramesPerBuffer = 512;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        AudioManager am = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (am != null) {
            String sr  = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
            String fpb = am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
            if (sr  != null) sOptimalSampleRate      = Integer.parseInt(sr);
            if (fpb != null) sOptimalFramesPerBuffer = Integer.parseInt(fpb);
        }
    }

    @Override
    protected String[] getLibraries() {
        // SDL2 and SDL2_mixer must be listed before the game library so they
        // are loaded first by the class loader.
        return new String[]{
            "SDL2",
            "SDL2_mixer",
            "newtonia"      // our CMake target
        };
    }

    @Override
    protected String getMainSharedObject() {
        return "libnewtonia.so";
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }
}
