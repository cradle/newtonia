#!/usr/bin/env python3
"""Exercise the production referrer callback with a fake Play service.

Needs Python 3 and Java 17; no Android SDK, emulator or Play account. Like
the web gesture harness, extracts the production methods between explicit
markers. Android CI still compiles the complete Activity. The fake callback
is synchronous: one-shot assertions cover sequential completed attempts, not
overlapping callbacks, Activity recreation or races with App Link intents.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ACTIVITY = ROOT / "android/app/src/main/java/org/newtonia/NewtoniaActivity.java"


def production_methods(source):
    begin = "// TEST-SLICE-BEGIN: android_install_referrer.py"
    end = "// TEST-SLICE-END: android_install_referrer.py"
    if source.count(begin) != 1 or source.count(end) != 1:
        raise ValueError(f"{ACTIVITY}: expected exactly one begin/end test marker")
    start = source.index(begin) + len(begin)
    stop = source.index(end)
    if stop <= start:
        raise ValueError(f"{ACTIVITY}: test markers are out of order")
    return source[start:stop]


HARNESS = r"""
import java.rmi.RemoteException;

class ReferrerTest {
    static class Context { static final int MODE_PRIVATE = 0; }
    static class SharedPreferences {
        boolean checked, persisted, failCommit;
        boolean getBoolean(String key, boolean fallback) { return checked; }
        SharedPreferences edit() { return this; }
        SharedPreferences putBoolean(String key, boolean value) {
            checked = value;
            return this;
        }
        void apply() {}
        boolean commit() {
            // Android updates the in-memory value even when disk commit fails.
            if (failCommit) return false;
            persisted = checked;
            return true;
        }
        void restart() { checked = persisted; }
    }
    final SharedPreferences prefs = new SharedPreferences();
    String joined;
    int deliveries;
    boolean failAfterDelivery, missingNativeLibrary;
    int nativeCalls;
    SharedPreferences getSharedPreferences(String name, int mode) { return prefs; }
    void nativeAcceptInvite(String code) {
        nativeCalls++;
        if (missingNativeLibrary) throw new UnsatisfiedLinkError("Library not loaded");
        check(prefs.persisted, "Consumption must reach disk before native handoff");
        joined = code;
        deliveries++;
        if (failAfterDelivery) throw new IllegalStateException("Failure after invite handoff");
    }

    interface InstallReferrerStateListener {
        void onInstallReferrerSetupFinished(int responseCode);
        void onInstallReferrerServiceDisconnected();
    }
    static class ReferrerDetails {
        String getInstallReferrer() { return InstallReferrerClient.referrer; }
    }
    static class InstallReferrerClient {
        static class InstallReferrerResponse {
            static final int OK = 0, SERVICE_UNAVAILABLE = 1, FEATURE_NOT_SUPPORTED = 2;
            static final int SERVICE_DISCONNECTED = -1;
        }
        static int response, connects, closes;
        static boolean failRead;
        static String referrer;
        static InstallReferrerClient newBuilder(Object context) {
            return new InstallReferrerClient();
        }
        InstallReferrerClient build() { return this; }
        void startConnection(InstallReferrerStateListener listener) {
            connects++;
            listener.onInstallReferrerSetupFinished(response);
        }
        ReferrerDetails getInstallReferrer() throws RemoteException {
            if (failRead) throw new RemoteException("Play service disconnected during read");
            return new ReferrerDetails();
        }
        void endConnection() { closes++; }
    }

    // PRODUCTION_METHODS

    static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    static ReferrerTest fresh(int response, boolean failRead, String referrer) {
        InstallReferrerClient.response = response;
        InstallReferrerClient.failRead = failRead;
        InstallReferrerClient.referrer = referrer;
        InstallReferrerClient.connects = InstallReferrerClient.closes = 0;
        return new ReferrerTest();
    }
    public static void main(String[] args) {
        ReferrerTest app = fresh(0, true, "code=ABC123");
        app.checkInstallReferrer();
        check(!app.prefs.checked, "A failed read must remain retryable");
        check(app.joined == null, "A failed read must not deliver an invite");
        check(InstallReferrerClient.closes == 1, "Close the failed connection");
        InstallReferrerClient.failRead = false;
        app.checkInstallReferrer();
        check(app.prefs.checked && "ABC123".equals(app.joined), "Retry must recover the invite");
        app.checkInstallReferrer();
        check(app.deliveries == 1 && InstallReferrerClient.connects == 2,
              "A successful invite must only be consumed once");
        check(InstallReferrerClient.closes == 2, "Close the successful connection");

        app = fresh(0, false, "code=ABC123");
        app.failAfterDelivery = true;
        app.checkInstallReferrer();
        app.checkInstallReferrer();
        check(app.prefs.checked && app.deliveries == 1 && InstallReferrerClient.connects == 1,
              "An exception after a successful read must not re-deliver the invite");
        check(InstallReferrerClient.closes == 1, "Close after a delivery exception");
        // Model process death after the handoff: lose all in-memory prefs.
        app.prefs.restart();
        app.checkInstallReferrer();
        check(app.deliveries == 1 && InstallReferrerClient.connects == 1,
              "A process restart after handoff must not re-deliver");

        app = fresh(0, false, "code=ABC123");
        app.missingNativeLibrary = true;
        app.checkInstallReferrer();
        app.prefs.restart();
        app.checkInstallReferrer();
        check(app.prefs.persisted && app.nativeCalls == 1 && app.deliveries == 0,
              "Missing JNI library must be swallowed and not retried");
        check(InstallReferrerClient.closes == 1, "Close after missing JNI library");

        // Direct App Links use the same guard without the referrer marker.
        app = fresh(0, false, "code=ABC123");
        app.missingNativeLibrary = true;
        app.acceptInviteSafely("ABC123");
        check(app.nativeCalls == 1 && !app.prefs.checked,
              "The shared JNI guard must also work without a referrer check");

        app = fresh(0, false, "code=ABC123");
        app.prefs.failCommit = true;
        app.checkInstallReferrer();
        check(!app.prefs.checked && !app.prefs.persisted && app.nativeCalls == 0,
              "Failed persistence must prevent delivery and restore retryable memory");
        check(InstallReferrerClient.closes == 1, "Close after persistence failure");
        app.prefs.failCommit = false;
        app.checkInstallReferrer();
        app.prefs.restart();
        app.checkInstallReferrer();
        check(app.prefs.persisted && app.deliveries == 1 && InstallReferrerClient.connects == 2,
              "Retry failed persistence and deliver only after a successful commit");

        // -1 is defensive API-code coverage, not evidence that the SDK emits
        // it through setup-finished. The read exception above covers a lost
        // connection during getInstallReferrer().
        for (int response : new int[]{1, -1}) {
            app = fresh(response, false, "code=ABC123");
            app.checkInstallReferrer();
            check(!app.prefs.checked, "Transient service response must remain retryable: " + response);
            check(app.deliveries == 0 && InstallReferrerClient.closes == 1,
                  "Close the transient connection without delivering an invite");
            InstallReferrerClient.response = 0;
            app.checkInstallReferrer();
            app.checkInstallReferrer();
            check("ABC123".equals(app.joined) && app.deliveries == 1 &&
                  InstallReferrerClient.connects == 2 && InstallReferrerClient.closes == 2,
                  "Retry a transient service response and consume the invite once");
        }

        for (String referrer : new String[]{"utm_source=newtonia_site", "code=TOO-LONG-CODE"}) {
            app = fresh(0, false, referrer);
            app.checkInstallReferrer();
            app.checkInstallReferrer();
            check(app.prefs.checked && app.joined == null && InstallReferrerClient.connects == 1,
                  "A successful read without a valid invite is definitive");
        }
        app = fresh(2, false, "code=ABC123");
        app.checkInstallReferrer();
        app.checkInstallReferrer();
        check(app.prefs.checked && app.joined == null && InstallReferrerClient.connects == 1,
              "An unsupported service is definitive");
        System.out.println("Install-referrer retry and one-shot checks passed");
    }
}
"""

source = ACTIVITY.read_text()
methods = production_methods(source)
with tempfile.TemporaryDirectory(prefix="newtonia-referrer-") as directory:
    harness = Path(directory) / "ReferrerTest.java"
    harness.write_text(HARNESS.replace("// PRODUCTION_METHODS", methods))
    subprocess.run(["java", str(harness)], check=True)
