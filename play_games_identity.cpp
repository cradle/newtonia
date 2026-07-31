// Google Play Games backend for the netplay peer-identity seam
// (net_identity.h, NETPLAY.md V2) — compiled only under
// __ANDROID__ && PLAY_GAMES_BUILD (the root CMakeLists sets both for every
// Android build, and defines NEWTONIA_NET_IDENTITY_BACKEND +
// NEWTONIA_NET_VERIFY_BACKEND so net_identity.cpp calls the functions below);
// an empty translation unit on every other platform, exactly like
// play_games_achievements.cpp.
//
// This is the Android analogue of the two Steam TUs combined into one:
//   - steam_identity.cpp        -> the display name + platform tag
//   - steam_identity_verify.cpp -> the credential the worker attests with
// Both concerns share the same Java bridge (PlayGamesIdentity.java), so one
// native TU is cleaner than the Steam split.
//
// The Play Games v2 SDK is Java-only, and both its player lookup and its
// server-auth-code mint are asynchronous, so the Java side fetches each into
// a cached field and this side just reads the cache over JNI:
//   - local_name()             -> the player's public display name (empty
//     until the async lookup resolves; pre-warmed at startup by
//     net_android_identity_init so it's ready before the lobby builds the
//     identity). Deliberately NEVER a Google account ID (net_identity.h,
//     XR-014) — only the display name Play Games already shows publicly.
//   - local_verify_credential()-> a SINGLE-USE OAuth server auth code from
//     requestServerSideAccess. Submitted client->worker over wss only (never
//     peer-to-peer), redeemed by the worker with our OAuth client SECRET
//     (play_games_verify.js) to prove the account and derive the attested
//     name server-side — so a lying wire `name` stops mattering.
//
// The p2p claimed name matters only on a worker-less (LAN) session; on an
// online session the worker attests the real name from the verified player,
// so an empty cache here is harmless online.
//
// Threading mirrors play_games_achievements.cpp: the game thread calls in via
// SDL_AndroidGetJNIEnv(); the Java side owns all its state on the UI thread.

#if defined(__ANDROID__) && defined(PLAY_GAMES_BUILD)

#include <jni.h>
#include <SDL.h>

#include <string>

#include "net_identity.h"

namespace {

// Log-and-clear any pending Java exception so a bridge failure can never take
// the game thread down (a JNI call made with an exception pending is undefined
// behaviour, and CheckJNI — on for every debuggable APK — turns it into an
// abort). Returns true if one was pending.
bool clear_exception(JNIEnv *env) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionDescribe();  // goes to logcat
  env->ExceptionClear();
  return true;
}

jclass g_bridge = NULL;            // global ref to PlayGamesIdentity
jmethodID g_init = NULL;           // static void init(Activity)
jmethodID g_display_name = NULL;   // static String displayName()
jmethodID g_server_code = NULL;    // static String serverAuthCode()
jmethodID g_server_code_peek = NULL; // static String peekServerAuthCode()
jmethodID g_release = NULL;        // static void release()

// Resolve the Java bridge class. FindClass normally works from the game
// thread (SDL_main runs beneath a Java frame, so the app class loader is in
// scope), but fall back to the activity's own class loader if it doesn't —
// the same belt-and-braces resolution play_games_achievements.cpp uses.
jclass resolve_bridge_class(JNIEnv *env, jobject activity) {
  jclass cls = env->FindClass("org/newtonia/PlayGamesIdentity");
  if (cls && !clear_exception(env)) return cls;
  clear_exception(env);
  jclass activity_class = env->GetObjectClass(activity);
  jmethodID get_loader = env->GetMethodID(activity_class, "getClassLoader",
                                          "()Ljava/lang/ClassLoader;");
  if (clear_exception(env) || !get_loader) {
    env->DeleteLocalRef(activity_class);
    return NULL;
  }
  jobject loader = env->CallObjectMethod(activity, get_loader);
  if (clear_exception(env) || !loader) {
    env->DeleteLocalRef(activity_class);
    return NULL;
  }
  jclass loader_class = env->GetObjectClass(loader);
  jmethodID load_class = env->GetMethodID(loader_class, "loadClass",
                                          "(Ljava/lang/String;)Ljava/lang/Class;");
  jstring name = NULL;
  if (!clear_exception(env) && load_class)
    name = env->NewStringUTF("org.newtonia.PlayGamesIdentity");
  cls = NULL;
  if (name && !clear_exception(env)) {
    cls = (jclass)env->CallObjectMethod(loader, load_class, name);
    if (clear_exception(env)) cls = NULL;
    env->DeleteLocalRef(name);
  }
  env->DeleteLocalRef(loader_class);
  env->DeleteLocalRef(loader);
  env->DeleteLocalRef(activity_class);
  return cls;
}

// Cache the bridge class + method IDs once. Returns true when the bridge is
// usable. A failed lookup leaves the bridge null and every call degrades to
// "" / no-op (identity simply stays a claim / no credential).
bool ensure_bridge(JNIEnv *env) {
  if (g_bridge) return true;
  jobject activity = (jobject)SDL_AndroidGetActivity();
  if (!activity) return false;
  jclass cls = resolve_bridge_class(env, activity);
  if (cls) {
    g_init = env->GetStaticMethodID(cls, "init", "(Landroid/app/Activity;)V");
    // A failed lookup leaves its exception PENDING; clear it before the next
    // JNI call or that call is undefined behaviour (CheckJNI abort).
    clear_exception(env);
    g_display_name = env->GetStaticMethodID(cls, "displayName",
                                            "()Ljava/lang/String;");
    clear_exception(env);
    g_server_code = env->GetStaticMethodID(cls, "serverAuthCode",
                                           "()Ljava/lang/String;");
    clear_exception(env);
    g_server_code_peek = env->GetStaticMethodID(cls, "peekServerAuthCode",
                                                "()Ljava/lang/String;");
    clear_exception(env);
    g_release = env->GetStaticMethodID(cls, "release", "()V");
    if (clear_exception(env) || !g_init || !g_display_name || !g_server_code ||
        !g_server_code_peek || !g_release) {
      g_init = g_display_name = g_server_code = g_server_code_peek =
          g_release = NULL;
    } else {
      g_bridge = (jclass)env->NewGlobalRef(cls);
    }
    env->DeleteLocalRef(cls);
  }
  env->DeleteLocalRef(activity);
  return g_bridge != NULL;
}

// Call a static no-arg String method on the bridge and copy it to std::string.
// `m` is taken by reference so it is read AFTER ensure_bridge() has populated
// the method-ID globals (on the first call they are still null when bound).
std::string call_string(jmethodID &m) {
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  if (!env || !ensure_bridge(env) || !m) return "";
  jstring s = (jstring)env->CallStaticObjectMethod(g_bridge, m);
  if (clear_exception(env) || !s) return "";
  const char *chars = env->GetStringUTFChars(s, NULL);
  std::string out = (chars && !clear_exception(env)) ? chars : "";
  if (chars) env->ReleaseStringUTFChars(s, chars);
  env->DeleteLocalRef(s);
  return out;
}

}  // namespace

// Startup warm hook, called from android_main.cpp after Achievements::init()
// (which brings up the Play Games SDK + automatic sign-in): hands the activity
// to the Java bridge and pre-fetches the display name so it is cached before
// the lobby ever builds net_local_identity(). Safe to call more than once (a
// kept-alive SDL_main re-entry) — the Java side just refreshes its reference.
//
// extern "C": android_main.cpp declares this at block scope inside the
// extern "C" SDL_main, which gives the reference C linkage — so the definition
// must have C linkage too, or the mangled/unmangled names don't match and the
// Android link fails with "undefined symbol: net_android_identity_init".
extern "C" void net_android_identity_init() {
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  jobject activity = (jobject)SDL_AndroidGetActivity();
  if (!env || !activity) return;
  if (ensure_bridge(env)) {
    env->CallStaticVoidMethod(g_bridge, g_init, activity);
    clear_exception(env);
  }
  env->DeleteLocalRef(activity);
}

namespace NetIdentityBackend {

uint8_t local_platform() { return NET_PLATFORM_ANDROID; }

// The Play Games display name, or "" until the async lookup resolves (the
// shared layer then sends a badge-only identity, and the worker attests the
// real name online anyway).
std::string local_name() { return call_string(g_display_name); }

// The most recently minted single-use server auth code, or "" if none has
// completed yet. The Java side re-mints a fresh one after each read (codes are
// single-use — the worker's token exchange consumes it), mirroring
// steam_identity_verify.cpp's per-call re-request.
std::string local_verify_credential() { return call_string(g_server_code); }

// Peek the cached code without consuming it or firing a fetch (see
// PlayGamesIdentity.peekServerAuthCode) — the upload retry polls this to
// wait for a fresh code before consuming one.
std::string local_verify_credential_peek() {
  return call_string(g_server_code_peek);
}

// Netplay teardown (~NetLobby / ~GLGame): drop any warmed-but-unsent code so a
// later session can't re-hand a stale one. There is no client-side handle to
// cancel (unlike Steam's CancelAuthTicket) — the code is proven or spent
// entirely server-side — so this only clears the Java cache.
void release_verify_credentials() {
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  if (!env || !ensure_bridge(env) || !g_release) return;
  env->CallStaticVoidMethod(g_bridge, g_release);
  clear_exception(env);
}

}  // namespace NetIdentityBackend

#endif  // __ANDROID__ && PLAY_GAMES_BUILD
