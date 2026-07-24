package org.newtonia;

// NewtoniaActivity extends SDL's SDLActivity.
// SDLActivity sets up the EGL surface, event loop bridge, and calls
// SDL_main() in the native library.

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.media.AudioManager;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.system.Os;
import android.view.Display;
import android.view.DisplayCutout;

import com.android.installreferrer.api.InstallReferrerClient;
import com.android.installreferrer.api.InstallReferrerStateListener;
import com.android.installreferrer.api.ReferrerDetails;

import org.libsdl.app.SDLActivity;

public class NewtoniaActivity extends SDLActivity {

    // Device's native audio output parameters, populated in onCreate() from
    // AudioManager so the native layer can open SDL2_mixer at the optimal
    // sample rate and buffer size for this hardware.
    static int sOptimalSampleRate      = 48000;
    static int sOptimalFramesPerBuffer = 512;

    // Display-cutout safe insets in physical px (the fullscreen surface
    // extends under the camera notch/punch-hole). Read by the native layer
    // like the audio params above; updated on rotation via
    // onConfigurationChanged, which android_main follows with a JNI re-read
    // on the SDL resize event.
    static int sSafeInsetTop = 0, sSafeInsetBottom = 0;
    static int sSafeInsetLeft = 0, sSafeInsetRight = 0;

    // Display.getCutout() (API 29+) already reflects the current rotation,
    // and is callable synchronously from any lifecycle point — unlike the
    // window insets, which need a listener and an attached window. Cutout
    // phones still on API 28 keep 0 insets (cosmetic only: the HUD row
    // stays at the screen edge there, as it always has).
    private void updateSafeInsets() {
        if (Build.VERSION.SDK_INT < 29) return;
        try {
            Display d = getWindowManager().getDefaultDisplay();
            DisplayCutout c = d != null ? d.getCutout() : null;
            sSafeInsetTop    = c != null ? c.getSafeInsetTop()    : 0;
            sSafeInsetBottom = c != null ? c.getSafeInsetBottom() : 0;
            sSafeInsetLeft   = c != null ? c.getSafeInsetLeft()   : 0;
            sSafeInsetRight  = c != null ? c.getSafeInsetRight()  : 0;
        } catch (Exception ignored) {
            // Best-effort: without the insets the HUD keeps its edge layout.
        }
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        updateSafeInsets();
    }

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

    // Deferred deep link (task #145): a join link tapped on a phone WITHOUT
    // the game routes to the Play Store with the room code riding the
    // install referrer (&referrer=code%3DXXXX on the store URL — the /join
    // page builds it). The Play Store preserves that string across the
    // install; on first launch we read it back and hand the code to the
    // same native invite path a tapped App Link uses. One-shot: a stored
    // flag stops every later launch from re-consuming a stale code, but a
    // TRANSIENT service failure leaves the flag unset so the next launch
    // retries (the referrer itself persists ~90 days server-side).
    private void checkInstallReferrer() {
        final SharedPreferences prefs =
            getSharedPreferences("newtonia", Context.MODE_PRIVATE);
        if (prefs.getBoolean("install_referrer_checked", false)) return;
        final InstallReferrerClient client =
            InstallReferrerClient.newBuilder(this).build();
        client.startConnection(new InstallReferrerStateListener() {
            @Override
            public void onInstallReferrerSetupFinished(int responseCode) {
                boolean definitive = true;
                try {
                    if (responseCode ==
                        InstallReferrerClient.InstallReferrerResponse.OK) {
                        ReferrerDetails details = client.getInstallReferrer();
                        String code = referrerCode(details.getInstallReferrer());
                        if (code != null) nativeAcceptInvite(code);
                    } else if (responseCode ==
                               InstallReferrerClient.InstallReferrerResponse
                                   .SERVICE_UNAVAILABLE) {
                        definitive = false;  // transient: retry next launch
                    }
                } catch (Exception ignored) {
                    // Referrer is best-effort; never let it disturb launch.
                } finally {
                    if (definitive) {
                        prefs.edit()
                            .putBoolean("install_referrer_checked", true)
                            .apply();
                    }
                    try { client.endConnection(); } catch (Exception ignored) {}
                }
            }
            @Override
            public void onInstallReferrerServiceDisconnected() {
                // Retry happens naturally on the next launch (flag unset).
            }
        });
    }

    // Extract a room code from a referrer string like "code=XXXX" (the Play
    // Store URL-decodes the %3D once) or "utm_source=...&code=XXXX". Codes
    // are short upper-alnum; anything else is not ours.
    private static String referrerCode(String referrer) {
        if (referrer == null) return null;
        for (String kv : referrer.split("&")) {
            int eq = kv.indexOf('=');
            if (eq <= 0) continue;
            if (!kv.substring(0, eq).equals("code")) continue;
            String v = kv.substring(eq + 1).trim().toUpperCase();
            if (v.matches("[A-Z0-9]{1,8}")) return v;
        }
        return null;
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

    // LAN discovery beacons (net_lan.cpp) carry a host name for the
    // joiner's row/band; native gethostname() is a bare "localhost" on
    // Android, so export the user-visible device name over the same env
    // bridge the adb debug extras use (native reads NEWTONIA_DEVICE_NAME
    // and sanitizes it into the game font). Skipped if the var is
    // already set — an adb --es NEWTONIA_DEVICE_NAME override wins.
    private void exportDeviceName() {
        try {
            if (Os.getenv("NEWTONIA_DEVICE_NAME") != null) return;
            String name = Settings.Global.getString(
                getContentResolver(), Settings.Global.DEVICE_NAME);
            if (name == null || name.isEmpty())
                name = Settings.Secure.getString(getContentResolver(),
                                                 "bluetooth_name");
            if (name == null || name.isEmpty()) name = Build.MODEL;
            if (name != null && !name.isEmpty())
                Os.setenv("NEWTONIA_DEVICE_NAME", name, true);
        } catch (Exception ignored) {
            // Best-effort: native falls back to its NEWTONIA default.
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        applyEnvExtras(getIntent());
        exportDeviceName();
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

        updateSafeInsets();

        // Cold launch from a tapped join link.
        handleInviteIntent(getIntent());

        // First launch after a Play Store install: recover a join code that
        // rode the install referrer (async; no-op on every later launch).
        // If this launch ALSO carried an App Link intent, both feed the
        // same pending-code slot and the menu poll drains one — the codes
        // are identical in that scenario, so order doesn't matter.
        checkInstallReferrer();
    }

    // Warm launch: singleTask (AndroidManifest) delivers a join link tapped
    // while the game is already running here instead of spawning a new task.
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleInviteIntent(intent);
    }

    // LAN co-op discovery (net_lan.cpp): Android wifi drivers filter
    // broadcast/multicast UDP unless a MulticastLock is held, so the
    // native beacon browse would hear nothing. Held only while the app
    // is foreground (acquire in onResume, release in onPause) — the
    // lobby is the only consumer and it never runs backgrounded. Needs
    // CHANGE_WIFI_MULTICAST_STATE (AndroidManifest, install-time grant).
    private WifiManager.MulticastLock multicastLock;

    @Override
    protected void onResume() {
        super.onResume();
        // Refresh the Play Games backend's activity reference (a recreated
        // activity resumes before the native side re-runs its init) and
        // retry sign-in / flush queued earns after backgrounding.
        PlayGamesAchievements.onResume(this);
        try {
            if (multicastLock == null) {
                WifiManager wm = (WifiManager)
                    getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                if (wm != null) {
                    multicastLock = wm.createMulticastLock("newtonia-lan");
                    multicastLock.setReferenceCounted(false);
                }
            }
            if (multicastLock != null) multicastLock.acquire();
        } catch (Exception ignored) {
            // Best-effort: without the lock LAN discovery may miss beacons
            // on some devices, but nothing else is affected.
        }
    }

    @Override
    protected void onPause() {
        try {
            if (multicastLock != null && multicastLock.isHeld())
                multicastLock.release();
        } catch (Exception ignored) {}
        super.onPause();
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
