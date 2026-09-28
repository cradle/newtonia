package org.newtonia;

// Java half of the Android cloud sync (cloud_sync.h): one Play Games saved
// game holds the bundle of savegame.dat, stats.dat and highscore.dat. The
// native half — android_cloud_sync.cpp — owns every merge decision; this
// class only moves bytes:
//   pull: open the saved game, hand its bytes to nativeCloudData, close it.
//   push: open it, replace its bytes with the bundle native passed, commit.
// It pulls at startup (init) and on every resume, and pushes whenever the
// native side's merge produced something the cloud lacks.
//
// Everything runs on the UI thread through a main-looper handler, one
// operation at a time (a saved game can only be open once): a pull wanted
// runs before a push waiting, since the pull's merge supersedes it, and a
// newer push replaces an older one still waiting. A failed operation is
// not retried on the spot — the next resume or local write tries again —
// so a device that is signed out, offline, or on a console with Saved
// Games switched off costs one log line, not a loop.
//
// Needs Saved Games switched on in the Play Console's Play Games Services
// configuration. PlayGamesSdk.initialize() is PlayGamesAchievements' job;
// that init is posted to the same handler earlier, so it has run by the
// time anything here does.

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.google.android.gms.games.AuthenticationResult;
import com.google.android.gms.games.PlayGames;
import com.google.android.gms.games.SnapshotsClient;
import com.google.android.gms.games.snapshot.Snapshot;
import com.google.android.gms.games.snapshot.SnapshotMetadataChange;
import com.google.android.gms.tasks.OnCompleteListener;
import com.google.android.gms.tasks.Task;

public final class PlayGamesSaves {

    private static final String TAG = "NewtoniaCloudSync";
    private static final String SNAPSHOT = "newtonia_progress";
    private static final int POLICY =
            SnapshotsClient.RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED;

    private static final Handler sUiHandler = new Handler(Looper.getMainLooper());

    private static volatile Activity sActivity;

    // UI thread only.
    private static boolean sBusy;
    private static boolean sPullWanted;
    private static byte[] sPush;
    private static boolean sAuthLogged;

    // android_cloud_sync.cpp: the saved game's bytes (empty = just created).
    private static native void nativeCloudData(byte[] data);

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
                sPush = data;
                pump();
            }
        });
    }

    private static void requestPull() {
        sUiHandler.post(new Runnable() {
            @Override public void run() {
                sPullWanted = true;
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
                            Log.i(TAG, "not signed in to Play Games, saves stay local");
                        }
                        done(false);
                        return;
                    }
                    sAuthLogged = false;
                    SnapshotsClient client = PlayGames.getSnapshotsClient(activity);
                    if (sPullWanted) {
                        sPullWanted = false;
                        pull(client);
                    } else {
                        byte[] data = sPush;
                        sPush = null;
                        push(client, data);
                    }
                }
            });
        } catch (Throwable t) {
            Log.w(TAG, "sign-in check failed", t);
            done(false);
        }
    }

    // UI thread only. ok: move on to whatever else is waiting now; after a
    // failure, wait for the next resume or write instead of spinning.
    private static void done(boolean ok) {
        sBusy = false;
        if (ok) pump();
    }

    private static void deliver(Snapshot s) throws java.io.IOException {
        byte[] data = s.getSnapshotContents().readFully();
        nativeCloudData(data != null ? data : new byte[0]);
    }

    private static void pull(final SnapshotsClient client) {
        try {
            client.open(SNAPSHOT, true, POLICY).addOnCompleteListener(
                    new OnCompleteListener<SnapshotsClient.DataOrConflict<Snapshot>>() {
                @Override public void onComplete(
                        Task<SnapshotsClient.DataOrConflict<Snapshot>> task) {
                    if (!task.isSuccessful()) {
                        Log.w(TAG, "open for read failed (is Saved Games on in the Play Console?)",
                              task.getException());
                        done(false);
                        return;
                    }
                    try {
                        SnapshotsClient.DataOrConflict<Snapshot> r = task.getResult();
                        if (r.isConflict()) {
                            // The policy normally resolves this itself. When
                            // it can't, hand both copies to the merge — its
                            // rules take the union — and keep the server's.
                            SnapshotsClient.SnapshotConflict c = r.getConflict();
                            deliver(c.getConflictingSnapshot());
                            deliver(c.getSnapshot());
                            resolve(client, c);
                            return;
                        }
                        Snapshot s = r.getData();
                        deliver(s);
                        client.discardAndClose(s);
                        done(true);
                    } catch (Throwable t) {
                        Log.w(TAG, "reading the saved game failed", t);
                        done(false);
                    }
                }
            });
        } catch (Throwable t) {
            Log.w(TAG, "open for read failed", t);
            done(false);
        }
    }

    private static void resolve(final SnapshotsClient client,
                                SnapshotsClient.SnapshotConflict c) {
        client.resolveConflict(c.getConflictId(), c.getSnapshot()).addOnCompleteListener(
                new OnCompleteListener<SnapshotsClient.DataOrConflict<Snapshot>>() {
            @Override public void onComplete(
                    Task<SnapshotsClient.DataOrConflict<Snapshot>> task) {
                try {
                    if (task.isSuccessful() && !task.getResult().isConflict())
                        client.discardAndClose(task.getResult().getData());
                } catch (Throwable t) {
                    Log.w(TAG, "closing the resolved saved game failed", t);
                }
                done(task.isSuccessful());
            }
        });
    }

    private static void push(final SnapshotsClient client, final byte[] data) {
        try {
            client.open(SNAPSHOT, true, POLICY).addOnCompleteListener(
                    new OnCompleteListener<SnapshotsClient.DataOrConflict<Snapshot>>() {
                @Override public void onComplete(
                        Task<SnapshotsClient.DataOrConflict<Snapshot>> task) {
                    if (!task.isSuccessful()) {
                        Log.w(TAG, "open for write failed", task.getException());
                        requeue(data);
                        done(false);
                        return;
                    }
                    try {
                        SnapshotsClient.DataOrConflict<Snapshot> r = task.getResult();
                        if (r.isConflict()) {
                            // Settle the conflict, then read before writing:
                            // the next pull merges and pushes afresh.
                            sPullWanted = true;
                            resolve(client, r.getConflict());
                            return;
                        }
                        Snapshot s = r.getData();
                        s.getSnapshotContents().writeBytes(data);
                        SnapshotMetadataChange meta = new SnapshotMetadataChange.Builder()
                                .setDescription("Newtonia progress").build();
                        client.commitAndClose(s, meta).addOnCompleteListener(
                                new OnCompleteListener<com.google.android.gms.games.snapshot.SnapshotMetadata>() {
                            @Override public void onComplete(
                                    Task<com.google.android.gms.games.snapshot.SnapshotMetadata> t) {
                                if (!t.isSuccessful()) {
                                    Log.w(TAG, "saving the saved game failed", t.getException());
                                    requeue(data);
                                }
                                done(t.isSuccessful());
                            }
                        });
                    } catch (Throwable t) {
                        Log.w(TAG, "writing the saved game failed", t);
                        requeue(data);
                        done(false);
                    }
                }
            });
        } catch (Throwable t) {
            Log.w(TAG, "open for write failed", t);
            requeue(data);
            done(false);
        }
    }

    // A newer push that arrived meanwhile wins over the one that failed.
    private static void requeue(byte[] data) {
        if (sPush == null) sPush = data;
    }
}
