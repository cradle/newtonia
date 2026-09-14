#!/usr/bin/env python3
"""Exercise the production referrer callback with a fake Play service.

Needs Python 3 and Java 17; no Android SDK, emulator or Play account. Like
the web gesture harness, extracts the production methods rather than copying
their implementation. Android CI still compiles the complete Activity.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ACTIVITY = ROOT / "android/app/src/main/java/org/newtonia/NewtoniaActivity.java"


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r"""
import java.rmi.RemoteException;

class ReferrerTest {
    static class Context { static final int MODE_PRIVATE = 0; }
    static class SharedPreferences {
        boolean checked;
        boolean getBoolean(String key, boolean fallback) { return checked; }
        SharedPreferences edit() { return this; }
        SharedPreferences putBoolean(String key, boolean value) {
            checked = value;
            return this;
        }
        void apply() {}
    }
    final SharedPreferences prefs = new SharedPreferences();
    String joined;
    int deliveries;
    SharedPreferences getSharedPreferences(String name, int mode) { return prefs; }
    void nativeAcceptInvite(String code) { joined = code; deliveries++; }

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

        app = fresh(1, false, "code=ABC123");
        app.checkInstallReferrer();
        check(!app.prefs.checked, "Service unavailable must remain retryable");
        InstallReferrerClient.response = 0;
        app.checkInstallReferrer();
        check("ABC123".equals(app.joined), "Retry unavailable service on next launch");

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
methods = "\n".join(method(source, signature) for signature in (
    "private void checkInstallReferrer()",
    "private static String referrerCode(String referrer)",
))
with tempfile.TemporaryDirectory(prefix="newtonia-referrer-") as directory:
    harness = Path(directory) / "ReferrerTest.java"
    harness.write_text(HARNESS.replace("// PRODUCTION_METHODS", methods))
    subprocess.run(["java", str(harness)], check=True)
