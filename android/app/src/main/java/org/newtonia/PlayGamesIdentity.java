package org.newtonia;

// Java half of the Google Play Games netplay identity backend
// (NETPLAY.md V2). The native half — play_games_identity.cpp, compiled under
// PLAY_GAMES_BUILD — calls the static entry points below over JNI from the
// game thread; every one either reads a cached field or posts async work to
// the UI thread, where all Play Games client state lives (same threading rule
// as PlayGamesAchievements).
//
// Two things are supplied to the identity seam:
//   - displayName(): the player's public Play Games display name, for the
//     lobby/HUD badge. Fetched via PlayersClient.getCurrentPlayer(); the
//     result is the ONLY thing that leaves this class — never the Google
//     player id (net_identity.h, XR-014).
//   - serverAuthCode(): a SINGLE-USE OAuth server auth code from
//     GamesSignInClient.requestServerSideAccess. It goes client->worker over
//     wss only; the signal worker redeems it with our OAuth client SECRET and
//     derives the VERIFIED player server-side (play_games_verify.js), which is
//     what promotes the peer's badge from a claim to an attestation.
//
// The Play Games SDK is initialised (and automatic sign-in kicked off) by
// PlayGamesAchievements.init(), which android_main.cpp runs BEFORE
// net_android_identity_init(); this class therefore never calls
// PlayGamesSdk.initialize() itself. If sign-in has not resolved yet a lookup
// just fails and the cache stays empty (badge-only claim / no credential) —
// graceful, exactly like Steam before the client is logged in.
//
// The OAuth WEB client id needed by requestServerSideAccess is a string
// resource (play_games_oauth_client_id in games-ids.xml). Absent/empty ==
// attestation disabled: no code is minted and the peer stays role-labelled —
// the same graceful degradation as the worker running without the Cloudflare
// secret. The source ships before the Google Cloud console value exists, the
// same way the achievement IDs did.

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.google.android.gms.games.GamesSignInClient;
import com.google.android.gms.games.PlayGames;
import com.google.android.gms.games.Player;
import com.google.android.gms.games.PlayersClient;
import com.google.android.gms.tasks.OnCompleteListener;
import com.google.android.gms.tasks.Task;

public final class PlayGamesIdentity {

    private static final String TAG = "NewtoniaPlayGames";

    private static final Handler sUiHandler = new Handler(Looper.getMainLooper());

    // Written by init() on the game thread, read on the UI thread.
    private static volatile Activity sActivity;

    // Caches, written on the UI thread (the Task callbacks fire on the main
    // thread), read (and, for the code, cleared) on the game thread via JNI.
    // Volatile so the game thread sees the UI thread's writes; a benign
    // read/write race only ever costs a single graceful "" (the value is
    // re-fetched on the next call).
    private static volatile String sDisplayName = "";
    private static volatile String sServerAuthCode = "";

    // UI-thread-only in-flight guards so repeated native reads don't stack
    // duplicate outstanding requests.
    private static boolean sNameInFlight;
    private static boolean sCodeInFlight;

    private PlayGamesIdentity() {}

    // Native entry point (game thread): cache the activity and pre-fetch the
    // display name so it is ready before the lobby builds the identity.
    public static void init(final Activity activity) {
        sActivity = activity;
        sUiHandler.post(new Runnable() {
            @Override public void run() { fetchName(); }
        });
    }

    // Native entry point (game thread, JNI): the cached Play Games display
    // name, "" until the async lookup resolves. Kicks a fetch if we don't have
    // one yet (covers a call that beats the init pre-warm).
    public static String displayName() {
        if (sDisplayName.isEmpty())
            sUiHandler.post(new Runnable() {
                @Override public void run() { fetchName(); }
            });
        return sDisplayName;
    }

    // Native entry point (game thread, JNI): the most recently minted
    // single-use server auth code (or "" if none has completed), and fire a
    // fresh request for next time. Codes are single-use — the worker's token
    // exchange consumes one — so the cached value is cleared on read and never
    // handed out twice (a re-host/rejoin gets its own), mirroring
    // steam_identity_verify.cpp.
    public static String serverAuthCode() {
        final String code = sServerAuthCode;
        sServerAuthCode = "";
        sUiHandler.post(new Runnable() {
            @Override public void run() { fetchCode(); }
        });
        return code;
    }

    // Native entry point (game thread, JNI): PEEK the cached code WITHOUT
    // consuming (clearing) it or firing a fetch — the leaderboard upload
    // retry polls this to wait for a fresh code after the first submit's
    // serverAuthCode() read already fired the next fetch. Merely comparing
    // values doesn't spend the code (only the worker's token exchange does),
    // so peeking is safe; the retry then calls serverAuthCode() ONCE to
    // consume the fresh one for the actual resubmit.
    public static String peekServerAuthCode() {
        return sServerAuthCode;
    }

    // Native entry point (game thread, JNI): netplay teardown drops a
    // warmed-but-unsent code so a later session can't re-hand a stale one.
    public static void release() {
        sServerAuthCode = "";
    }

    // UI thread.
    private static void fetchName() {
        final Activity activity = sActivity;
        if (activity == null || sNameInFlight || !sDisplayName.isEmpty()) return;
        sNameInFlight = true;
        try {
            PlayersClient players = PlayGames.getPlayersClient(activity);
            players.getCurrentPlayer().addOnCompleteListener(
                    new OnCompleteListener<Player>() {
                @Override public void onComplete(Task<Player> task) {
                    sNameInFlight = false;
                    if (task.isSuccessful() && task.getResult() != null) {
                        String n = task.getResult().getDisplayName();
                        if (n != null && !n.isEmpty()) sDisplayName = n;
                    }
                    // Not signed in yet / lookup failed: leave the cache empty;
                    // a later call (or onResume-driven sign-in) retries.
                }
            });
        } catch (Throwable t) {
            sNameInFlight = false;
            Log.w(TAG, "Play Games player-name lookup failed", t);
        }
    }

    // UI thread.
    private static void fetchCode() {
        final Activity activity = sActivity;
        if (activity == null || sCodeInFlight) return;
        String clientId = oauthClientId(activity);
        if (clientId == null) return;  // not configured: attestation disabled
        sCodeInFlight = true;
        try {
            GamesSignInClient signIn = PlayGames.getGamesSignInClient(activity);
            // forceRefreshToken=false: we only need the one-shot auth code, not
            // a refreshed OAuth refresh token.
            signIn.requestServerSideAccess(clientId, false).addOnCompleteListener(
                    new OnCompleteListener<String>() {
                @Override public void onComplete(Task<String> task) {
                    sCodeInFlight = false;
                    if (task.isSuccessful()) {
                        String c = task.getResult();
                        if (c != null && !c.isEmpty()) sServerAuthCode = c;
                    }
                }
            });
        } catch (Throwable t) {
            sCodeInFlight = false;
            Log.w(TAG, "Play Games server-side access request failed", t);
        }
    }

    // The OAuth 2.0 WEB client id for this Play Games project (Google Cloud
    // console — NOT the Android client id). requestServerSideAccess mints an
    // auth code redeemable only with the matching client SECRET, held by the
    // signal worker (play_games_verify.js). Absent/empty resource == no
    // attestation, gracefully.
    private static String oauthClientId(Activity activity) {
        try {
            int rid = activity.getResources().getIdentifier(
                    "play_games_oauth_client_id", "string",
                    activity.getPackageName());
            if (rid == 0) return null;
            String v = activity.getString(rid);
            return (v != null && !v.isEmpty()) ? v : null;
        } catch (Throwable t) {
            return null;
        }
    }
}
