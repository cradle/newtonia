package org.newtonia;

// NewtoniaActivity extends SDL's SDLActivity.
// SDLActivity sets up the EGL surface, event loop bridge, and calls
// SDL_main() in the native library.

import android.content.Context;
import android.media.AudioManager;
import android.os.Bundle;
import android.util.Log;

import com.google.firebase.analytics.FirebaseAnalytics;

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

        // Google Analytics for Firebase. Touching the instance ensures the SDK
        // is initialised so automatic events (first_open, session_start,
        // user_engagement) are collected. Guarded so the app still launches if
        // google-services.json has not been added yet (Firebase not configured).
        try {
            FirebaseAnalytics.getInstance(this);
        } catch (Exception e) {
            Log.w("Newtonia", "Firebase Analytics not configured: " + e.getMessage());
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
