package org.newtonia;

// Saved Games live in the player's Drive app folder (OAuth scope
// drive.appdata). A Play Games profile made before Saved Games was switched
// on (2026-09-28) never granted it, and the Play Games sign-in library has no
// working way to ask again: on a real old profile (2026-10-05) the scoped
// requestServerSideAccess failed inside the service with a bare
// NullPointerException and the plain one with "Not signed in when calling
// API". This asks for the scope through Google's general authorization API
// instead, identified by the same package and signing key Play Games uses.
// Whether Play Games then honours the grant is the open question (TODO.md).

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Intent;
import android.util.Log;

import com.google.android.gms.auth.api.identity.AuthorizationRequest;
import com.google.android.gms.auth.api.identity.AuthorizationResult;
import com.google.android.gms.auth.api.identity.Identity;
import com.google.android.gms.common.api.Scope;
import com.google.android.gms.tasks.OnCompleteListener;
import com.google.android.gms.tasks.Task;

import java.util.Collections;

public final class SavedGamesConsent {

    private static final String TAG = "NewtoniaCloudSync";
    private static final String DRIVE_APPDATA =
            "https://www.googleapis.com/auth/drive.appdata";
    static final int REQUEST_CODE = 0x5A7E;

    // UI thread only: finishes the request, run once.
    private static Runnable sAfter;

    private SavedGamesConsent() {}

    // UI thread. `after` runs once the request settles, granted or not.
    // False when no request was started.
    public static boolean request(final Activity activity, Runnable after) {
        if (sAfter != null) return false;
        try {
            AuthorizationRequest req = AuthorizationRequest.builder()
                    .setRequestedScopes(Collections.singletonList(new Scope(DRIVE_APPDATA)))
                    .build();
            sAfter = after;
            Log.i(TAG, "asking Google for Drive app-folder access");
            Identity.getAuthorizationClient(activity).authorize(req)
                    .addOnCompleteListener(new OnCompleteListener<AuthorizationResult>() {
                @Override public void onComplete(Task<AuthorizationResult> task) {
                    if (!task.isSuccessful()) {
                        Log.w(TAG, "Drive access request failed", task.getException());
                        finish();
                        return;
                    }
                    AuthorizationResult r = task.getResult();
                    if (!r.hasResolution()) {
                        Log.i(TAG, "Drive access already granted: " + r.getGrantedScopes());
                        finish();
                        return;
                    }
                    try {
                        PendingIntent pi = r.getPendingIntent();
                        activity.startIntentSenderForResult(pi.getIntentSender(),
                                REQUEST_CODE, null, 0, 0, 0);
                    } catch (Throwable t) {
                        Log.w(TAG, "could not show the Drive access screen", t);
                        finish();
                    }
                }
            });
            return true;
        } catch (Throwable t) {
            Log.w(TAG, "Drive access request could not start", t);
            sAfter = null;
            return false;
        }
    }

    // NewtoniaActivity.onActivityResult.
    static void onActivityResult(Activity activity, int resultCode, Intent data) {
        try {
            AuthorizationResult r = Identity.getAuthorizationClient(activity)
                    .getAuthorizationResultFromIntent(data);
            Log.i(TAG, "Drive access screen finished (result " + resultCode
                    + "), granted " + r.getGrantedScopes());
        } catch (Throwable t) {
            Log.w(TAG, "Drive access screen declined or failed (result "
                    + resultCode + ")", t);
        }
        finish();
    }

    private static void finish() {
        Runnable r = sAfter;
        sAfter = null;
        if (r != null) r.run();
    }
}
