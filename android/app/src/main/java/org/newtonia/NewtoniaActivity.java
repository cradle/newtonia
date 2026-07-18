package org.newtonia;

// NewtoniaActivity extends SDL's SDLActivity.
// SDLActivity sets up the EGL surface, event loop bridge, and calls
// SDL_main() in the native library.

import android.content.Context;
import android.content.Intent;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Bundle;
import android.system.Os;

import org.libsdl.app.SDLActivity;

public class NewtoniaActivity extends SDLActivity {

    // Device's native audio output parameters, populated in onCreate() from
    // AudioManager so the native layer can open SDL2_mixer at the optimal
    // sample rate and buffer size for this hardware.
    static int sOptimalSampleRate      = 48000;
    static int sOptimalFramesPerBuffer = 512;

    // Hands a co-op join link's room code to the native invite layer
    // (android_main.cpp → Invites::note_accepted). super.onCreate has already
    // loaded libnewtonia by the time we call this, so the symbol resolves.
    private static native void nativeAcceptInvite(String code);

    // Pull ?code= out of a https://newtonia.metonymous.com/join?code=XXXX
    // App Link intent and forward it. Safe to call with any intent.
    private void handleInviteIntent(Intent intent) {
        if (intent == null) return;
        Uri data = intent.getData();
        if (data == null) return;
        String code = data.getQueryParameter("code");
        if (code != null && !code.isEmpty()) {
            nativeAcceptInvite(code);
        }
    }

    // Debug bridge: Android processes fork from zygote, so `adb shell` env
    // vars never reach the app. Intent extras named NEWTONIA_* are copied
    // into this process's environment instead, making every desktop debug
    // knob (NEWTONIA_BETA, NEWTONIA_START_GENERATION, NEWTONIA_ALL_WEAPONS,
    // ...) reachable on device:
    //   adb shell am start -S -n org.newtonia/.NewtoniaActivity \
    //       --es NEWTONIA_BETA 1 --es NEWTONIA_START_GENERATION 9
    // (-S force-stops first so a FRESH process reads the extras; a warm
    // resume keeps its old environment.) Runs before super.onCreate so the
    // env is set before any native code can read it. getenv is what the
    // native layer uses, so no JNI is needed.
    private void applyEnvExtras(Intent intent) {
        if (intent == null || intent.getExtras() == null) return;
        Bundle extras = intent.getExtras();
        for (String key : extras.keySet()) {
            if (!key.startsWith("NEWTONIA_")) continue;
            Object v = extras.get(key);
            if (v == null) continue;
            try {
                Os.setenv(key, String.valueOf(v), true);
            } catch (Exception ignored) {}  // ErrnoException: skip, don't crash
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        applyEnvExtras(getIntent());
        super.onCreate(savedInstanceState);

        AudioManager am = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (am != null) {
            String sr  = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
            String fpb = am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
            // OEM audio HALs supply these strings; a malformed value must
            // fall back to the defaults, not crash onCreate.
            try {
                if (sr  != null) sOptimalSampleRate      = Integer.parseInt(sr);
                if (fpb != null) sOptimalFramesPerBuffer = Integer.parseInt(fpb);
            } catch (NumberFormatException ignored) {}
        }

        // Cold launch from a tapped join link.
        handleInviteIntent(getIntent());
    }

    // Warm launch: singleTask (AndroidManifest) delivers a join link tapped
    // while the game is already running here instead of spawning a new task.
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleInviteIntent(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Refresh the Play Games backend's activity reference (a recreated
        // activity resumes before the native side re-runs its init) and
        // retry sign-in / flush queued earns after backgrounding.
        PlayGamesAchievements.onResume(this);
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
