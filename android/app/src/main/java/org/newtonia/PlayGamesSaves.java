package org.newtonia;

// Java half of the Android cloud sync (cloud_sync.h): one Play Games saved
// game holds the bundle of savegame.dat, stats.dat and highscore.dat. The
// native half — android_cloud_sync.cpp — owns every merge decision; this
// class moves bytes, folding them with nativeMergeBundles (the same
// max/OR/newest-stamp rules as the local merge, pure and thread-safe):
//   pull: open the saved game, hand its bytes to nativeCloudData, close it.
//   push: open it, fold what it holds INTO the bundle native passed, commit
//         the fold — so a bundle built from an older read never overwrites
//         progress another device uploaded since, and a stale queued push
//         is harmless.
// Conflicts are resolved manually by folding both versions together
// (RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED would discard one side whole).
// It pulls at startup (init) and on every resume, and pushes whenever the
// native side's merge produced something the cloud lacks.
//
// Everything runs on the UI thread through a main-looper handler, one
// operation at a time (a saved game can only be open once): a pull wanted
// runs before a push waiting, since the pull's merge supersedes it, and a
// newer push folds into one still waiting. A failed operation (sign-in
// still settling at launch, offline, Saved Games off) is retried on a
// backing-off timer for about two minutes, then waits for the next resume
// or local write — a bounded handful of log lines, never a loop. Each
// successful read and write logs one line (tag NewtoniaCloudSync).
//
// Needs Saved Games switched on in the Play Console's Play Games Services
// configuration. PlayGamesSdk.initialize() is PlayGamesAchievements' job;
// that init is posted to the same handler earlier, so it has run by the
// time anything here does.

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.util.Arrays;

import com.google.android.gms.games.AuthenticationResult;
import com.google.android.gms.games.PlayGames;
import com.google.android.gms.games.SnapshotsClient;
import com.google.android.gms.games.snapshot.Snapshot;
import com.google.android.gms.games.snapshot.SnapshotContents;
import com.google.android.gms.games.snapshot.SnapshotMetadata;
import com.google.android.gms.games.snapshot.SnapshotMetadataChange;
import com.google.android.gms.tasks.OnCompleteListener;
import com.google.android.gms.tasks.Task;

public final class PlayGamesSaves {

    private static final String TAG = "NewtoniaCloudSync";
    private static final String SNAPSHOT = "newtonia_progress";
    private static final int POLICY = SnapshotsClient.RESOLUTION_POLICY_MANUAL;
    // A resolution can itself meet a newer conflict; give up past this and
    // let the next resume or write try again.
    private static final int MAX_RESOLVE_ROUNDS = 4;

    private static final Handler sUiHandler = new Handler(Looper.getMainLooper());

    private static volatile Activity sActivity;

    // UI thread only.
    private static boolean sBusy;
    private static boolean sPullWanted;
    private static byte[] sPush;
    private static boolean sAuthLogged;
    // Retries after a failure: sign-in often completes a few seconds after
    // launch, and a lost startup pull would leave the whole run unsynced
    // (native pushes nothing until a copy has arrived).
    private static final long[] RETRY_MS = {2000, 5000, 15000, 30000, 60000};
    private static int sRetry;
    private static boolean sRetryPosted;

    // android_cloud_sync.cpp: the saved game's bytes (empty = just created).
    private static native void nativeCloudData(byte[] data);
    // android_cloud_sync.cpp: two bundles folded into one. Any thread.
    private static native byte[] nativeMergeBundles(byte[] a, byte[] b);

    private PlayGamesSaves() {}

    // Native entry point (game thread), from CloudSync::init().
    public static void init(Activity activity) {
        sActivity = activity;
        requestPull();
    }

    // NewtoniaActivity.onResume(): another device may have pushed while the
    // game was away.
    public static void onResume(Activity activity) {
        sActivity = activity;
        requestPull();
    }

    // Native entry point (game thread): the bundle to store.
    public static void push(final byte[] data) {
        sUiHandler.post(new Runnable() {
            @Override public void run() {
                sPush = sPush == null ? data : merge(sPush, data);
                sRetry = 0;
                pump();
            }
        });
    }

    private static void requestPull() {
        sUiHandler.post(new Runnable() {
            @Override public void run() {
                sPullWanted = true;
                sRetry = 0;
                pump();
            }
        });
    }

    // UI thread only: start the next operation if none is running.
    private static void pump() {
        if (sBusy || (!sPullWanted && sPush == null)) return;
        final Activity activity = sActivity;
        if (activity == null) return;
        sBusy = true;
        try {
            // Executor-less listener on purpose, as in PlayGamesAchievements:
            // the activity-scoped overload detaches at onStop and would wedge
            // sBusy for a check that completes in the background.
            PlayGames.getGamesSignInClient(activity).isAuthenticated()
                    .addOnCompleteListener(new OnCompleteListener<AuthenticationResult>() {
                @Override public void onComplete(Task<AuthenticationResult> task) {
                    boolean ok = task.isSuccessful()
                            && task.getResult().isAuthenticated();
                    if (!ok) {
                        if (!sAuthLogged) {
                            sAuthLogged = true;
                            Log.i(TAG, "not signed in to Play Games yet, will retry");
                        }
                        done(false);
                        return;
                    }
                    sAuthLogged = false;
                    SnapshotsClient client = PlayGames.getSnapshotsClient(activity);
                    if (sPullWanted) {
                        sPullWanted = false;
                        open(client, null);
                    } else {
                        byte[] data = sPush;
                        sPush = null;
                        open(client, data);
                    }
                }
            });
        } catch (Throwable t) {
            Log.w(TAG, "sign-in check failed", t);
            done(false);
        }
    }

    // UI thread only. ok: move on to whatever else is waiting now. After a
    // failure, retry on a backing-off timer; once that runs out, the next
    // resume or local write tries again.
    private static void done(boolean ok) {
        sBusy = false;
        if (ok) {
            sRetry = 0;
            pump();
            return;
        }
        if (sRetryPosted || sRetry >= RETRY_MS.length) return;
        if (!sPullWanted && sPush == null) return;
        sRetryPosted = true;
        sUiHandler.postDelayed(new Runnable() {
            @Override public void run() {
                sRetryPosted = false;
                pump();
            }
        }, RETRY_MS[sRetry++]);
    }

    private static byte[] contents(Snapshot s) throws java.io.IOException {
        byte[] data = s.getSnapshotContents().readFully();
        return data != null ? data : new byte[0];
    }

    // Never throws: the push path runs it in bare Runnables. If the fold
    // can't run, b (the newer side at every call) stands alone.
    private static byte[] merge(byte[] a, byte[] b) {
        try {
            byte[] m = nativeMergeBundles(a, b);
            if (m != null) return m;
        } catch (Throwable t) {
            Log.w(TAG, "bundle merge failed", t);
        }
        return b;
    }

    private static SnapshotMetadataChange meta() {
        return new SnapshotMetadataChange.Builder()
                .setDescription("Newtonia progress").build();
    }

    // UI thread only. push null = a pull: read, deliver, close.
    private static void open(final SnapshotsClient client, final byte[] push) {
        try {
            client.open(SNAPSHOT, true, POLICY).addOnCompleteListener(
                    new OnCompleteListener<SnapshotsClient.DataOrConflict<Snapshot>>() {
                @Override public void onComplete(
                        Task<SnapshotsClient.DataOrConflict<Snapshot>> task) {
                    if (!task.isSuccessful()) {
                        Log.w(TAG, "opening the saved game failed (is Saved Games on in the Play Console?)",
                              task.getException());
                        failed(push);
                        return;
                    }
                    opened(client, task.getResult(), push, 0);
                }
            });
        } catch (Throwable t) {
            Log.w(TAG, "opening the saved game failed", t);
            failed(push);
        }
    }

    // An open (or a resolution) came back: fold and settle it.
    private static void opened(final SnapshotsClient client,
                               SnapshotsClient.DataOrConflict<Snapshot> r,
                               final byte[] push, final int round) {
        try {
            if (r.isConflict()) {
                resolve(client, r.getConflict(), push, round);
                return;
            }
            Snapshot s = r.getData();
            final byte[] current = contents(s);
            final byte[] merged = push == null ? current : merge(current, push);
            if (Arrays.equals(merged, current)) {
                // The cloud already holds everything: nothing to write.
                Log.i(TAG, (push == null ? "read the saved game, " : "saved game already current, ")
                        + current.length + " bytes");
                deliver(current);
                client.discardAndClose(s);
                done(true);
                return;
            }
            s.getSnapshotContents().writeBytes(merged);
            client.commitAndClose(s, meta()).addOnCompleteListener(
                    new OnCompleteListener<SnapshotMetadata>() {
                @Override public void onComplete(Task<SnapshotMetadata> t) {
                    if (!t.isSuccessful()) {
                        Log.w(TAG, "saving the saved game failed", t.getException());
                        failed(push);
                        return;
                    }
                    Log.i(TAG, "saved the saved game, " + merged.length + " bytes");
                    deliver(merged);
                    done(true);
                }
            });
        } catch (Throwable t) {
            Log.w(TAG, "reading or writing the saved game failed", t);
            failed(push);
        }
    }

    // Two devices wrote the saved game apart: keep both — fold the two
    // versions (and any bundle waiting to go) and resolve with the fold.
    private static void resolve(final SnapshotsClient client,
                                SnapshotsClient.SnapshotConflict c,
                                final byte[] push, final int round)
            throws java.io.IOException {
        if (round >= MAX_RESOLVE_ROUNDS) {
            Log.w(TAG, "saved game still conflicted after " + round + " resolutions");
            failed(push);
            return;
        }
        byte[] merged = merge(contents(c.getSnapshot()), contents(c.getConflictingSnapshot()));
        if (push != null) merged = merge(merged, push);
        SnapshotContents out = c.getResolutionSnapshotContents();
        out.writeBytes(merged);
        final byte[] folded = merged;
        client.resolveConflict(c.getConflictId(),
                c.getSnapshot().getMetadata().getSnapshotId(), meta(), out)
                .addOnCompleteListener(
                new OnCompleteListener<SnapshotsClient.DataOrConflict<Snapshot>>() {
            @Override public void onComplete(
                    Task<SnapshotsClient.DataOrConflict<Snapshot>> task) {
                if (!task.isSuccessful()) {
                    Log.w(TAG, "resolving the saved game conflict failed", task.getException());
                    failed(push);
                    return;
                }
                // The resolved copy (or a newer conflict) goes round again;
                // the fold is already in it, so re-folding is a no-op.
                opened(client, task.getResult(), folded, round + 1);
            }
        });
    }

    private static void deliver(byte[] data) {
        nativeCloudData(data);
    }

    // A push that failed waits for the next resume or write; folding makes
    // a late retry safe even after newer progress lands.
    private static void failed(byte[] push) {
        // pump() cleared the want before opening: put a failed pull back,
        // or the startup read is lost and this run never syncs.
        if (push == null) sPullWanted = true;
        if (push != null) {
            // Merge into whatever newer push arrived meanwhile.
            sPush = sPush == null ? push : merge(sPush, push);
        }
        done(false);
    }
}
