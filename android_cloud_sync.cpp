// Play Games backend for CloudSync (cloud_sync.h): savegame.dat, stats.dat
// and highscore.dat roam through one Play Games saved game, carried as a
// CloudMerge bundle (cloud_sync_merge.h). Compiled only for the Android
// build (PLAY_GAMES_BUILD, set by the root CMakeLists.txt); the Java half is
// android/app/src/main/java/org/newtonia/PlayGamesSaves.java, which owns
// the Snapshots API calls and serialises them on the UI thread.
//
// Needs Saved Games switched on in the Play Console's Play Games Services
// configuration. Without it, or signed out, every open fails soft (logcat
// only) and the game plays on its local files as before.
//
// Flow. The Java side opens the saved game at startup and on every resume
// and hands its bytes to nativeCloudData (UI thread → inbox). poll() drains
// the inbox on the game thread, merges each copy into the local files with
// the shared driver (cloud_sync_core.h) against an in-memory copy of the
// bundle, and pushes the result back when the merge changed it. Local
// writes push the same way — but only once a copy has arrived this run:
// pushing before that would replace the cloud's files with this device's
// alone. Anything written before then is merged in when the copy lands.
//
// A snapshot written by another device between this device's read and its
// next write is overwritten (the open resolves to the most recently
// modified copy); that device still holds its files, and its next sync
// finds the cloud behind and pushes them again — high score and stats by
// max, the save by its newer stamp.

#if defined(__ANDROID__) && defined(PLAY_GAMES_BUILD)

#include <jni.h>
#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "atomic_file.h"
#include "cloud_sync.h"
#include "cloud_sync_core.h"
#include "cloud_sync_merge.h"
#include "stats.h"

namespace {

const int N = CloudMerge::BUNDLE_FILES;

// The cloud copy as last read or merged. Game thread only.
std::string g_val[N];
bool g_has[N] = {false, false, false};
bool g_have_copy = false;  // a copy arrived this run
bool g_dirty = false;      // g_val differs from what the cloud holds

bool g_started = false;
uint32_t g_generation = 0;

// Copies delivered by the Java side (UI thread), drained by poll().
std::mutex g_inbox_mutex;
std::vector<std::string> g_inbox;
std::atomic<bool> g_pending(false);

jclass g_bridge = NULL;     // global ref to PlayGamesSaves
jmethodID g_init = NULL;    // static void init(Activity)
jmethodID g_push = NULL;    // static void push(byte[])

bool clear_exception(JNIEnv *env) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionDescribe();
  env->ExceptionClear();
  return true;
}

// FindClass works from the game thread (SDL_main runs under a Java frame);
// fall back to the activity's class loader if it ever doesn't, as the
// achievements bridge does. Every step checks for a pending exception:
// a JNI call made with one pending aborts under CheckJNI.
jclass resolve_bridge_class(JNIEnv *env, jobject activity) {
  jclass cls = env->FindClass("org/newtonia/PlayGamesSaves");
  if (cls && !clear_exception(env)) return cls;
  clear_exception(env);
  jclass activity_class = env->GetObjectClass(activity);
  jmethodID get_loader = env->GetMethodID(activity_class, "getClassLoader",
                                          "()Ljava/lang/ClassLoader;");
  jobject loader = NULL;
  if (!clear_exception(env) && get_loader)
    loader = env->CallObjectMethod(activity, get_loader);
  cls = NULL;
  if (loader && !clear_exception(env)) {
    jclass loader_class = env->GetObjectClass(loader);
    jmethodID load_class = env->GetMethodID(
        loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = NULL;
    if (!clear_exception(env) && load_class)
      name = env->NewStringUTF("org.newtonia.PlayGamesSaves");
    if (name && !clear_exception(env)) {
      cls = (jclass)env->CallObjectMethod(loader, load_class, name);
      if (clear_exception(env)) cls = NULL;
      env->DeleteLocalRef(name);
    }
    env->DeleteLocalRef(loader_class);
    env->DeleteLocalRef(loader);
  }
  clear_exception(env);
  env->DeleteLocalRef(activity_class);
  return cls;
}

std::string stamp_path() {
  char *dir = SDL_GetPrefPath("cc.gfm", "newtonia");
  if (!dir) return "";
  std::string p = std::string(dir) + "cloud_sync_stamp.dat";
  SDL_free(dir);
  return p;
}

// The cloud side is the in-memory bundle; the savegame stamp lives in a
// small pref-path file (the manifest's allowBackup=false keeps it, like
// every other pref-path file, off Android's own backups).
struct BundleStore : CloudCore::Store {
  bool read(int f, std::string &out) override {
    out = g_has[f] ? g_val[f] : std::string();
    return g_has[f];
  }
  void write(int f, const std::string &bytes) override {
    if (g_has[f] && g_val[f] == bytes) return;
    g_val[f] = bytes;
    g_has[f] = true;
    g_dirty = true;
  }
  bool load_save_stamp(uint64_t &out) override {
    std::string path = stamp_path();
    FILE *fp = path.empty() ? NULL : fopen(path.c_str(), "rb");
    if (!fp) return false;
    bool ok = fread(&out, sizeof(out), 1, fp) == 1;
    fclose(fp);
    return ok;
  }
  void store_save_stamp(uint64_t stamp) override {
    std::string path = stamp_path();
    if (path.empty()) return;
    AtomicFile::write(path, [&](FILE *fp) {
      return fwrite(&stamp, sizeof(stamp), 1, fp) == 1;
    }, "cloud-sync stamp");
  }
};

BundleStore g_store;

// Hand the bundle to the Java side when it holds something the cloud
// doesn't, and a copy has arrived to merge against.
void push_if_dirty() {
  if (!g_dirty || !g_have_copy || !g_bridge) return;
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  if (!env) return;
  std::string b = CloudMerge::bundle_bytes(g_val, g_has);
  jbyteArray arr = env->NewByteArray((jsize)b.size());
  if (!arr || clear_exception(env)) return;
  env->SetByteArrayRegion(arr, 0, (jsize)b.size(), (const jbyte *)b.data());
  if (!clear_exception(env)) {
    env->CallStaticVoidMethod(g_bridge, g_push, arr);
    if (!clear_exception(env)) g_dirty = false;
  }
  env->DeleteLocalRef(arr);
}

}  // namespace

// PlayGamesSaves.nativeCloudData (UI thread): the saved game's bytes, empty
// for one just created.
extern "C" JNIEXPORT void JNICALL
Java_org_newtonia_PlayGamesSaves_nativeCloudData(JNIEnv *env, jclass,
                                                 jbyteArray data) {
  std::string b;
  if (data) {
    jsize n = env->GetArrayLength(data);
    b.resize((size_t)n);
    if (n > 0) env->GetByteArrayRegion(data, 0, n, (jbyte *)&b[0]);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return;
    }
  }
  std::lock_guard<std::mutex> lock(g_inbox_mutex);
  g_inbox.push_back(b);
  g_pending = true;
}

namespace CloudSync {

// After SDL_Init and Achievements::init (which brings up the Play Games
// SDK the Java side relies on). The first copy arrives asynchronously, so
// nothing merges here; poll() does it when the copy lands.
void init() {
  if (g_started) return;
  g_started = true;
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  jobject activity = (jobject)SDL_AndroidGetActivity();
  if (!env || !activity) {
    SDL_Log("cloud-sync: no JNI activity, sync off");
    return;
  }
  jclass cls = resolve_bridge_class(env, activity);
  if (cls) {
    g_init = env->GetStaticMethodID(cls, "init", "(Landroid/app/Activity;)V");
    clear_exception(env);
    g_push = env->GetStaticMethodID(cls, "push", "([B)V");
    if (!clear_exception(env) && g_init && g_push)
      g_bridge = (jclass)env->NewGlobalRef(cls);
    env->DeleteLocalRef(cls);
  }
  if (g_bridge) {
    env->CallStaticVoidMethod(g_bridge, g_init, activity);
    clear_exception(env);
  } else {
    SDL_Log("cloud-sync: PlayGamesSaves bridge unavailable, sync off");
  }
  env->DeleteLocalRef(activity);
}

bool poll() {
  if (!g_started || !g_pending.exchange(false)) return false;
  std::vector<std::string> copies;
  {
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    copies.swap(g_inbox);
  }
  // Pending in-memory counters go to disk first, so the merge sees them.
  Stats::flush();
  unsigned changed = 0;
  for (size_t i = 0; i < copies.size(); i++) {
    std::string v[N];
    bool has[N];
    if (!CloudMerge::parse_bundle(copies[i], v, has)) {
      SDL_Log("cloud-sync: saved game unreadable (%u bytes), ignored",
              (unsigned)copies[i].size());
      continue;
    }
    // The cloud's copy replaces whatever this run held; the local files
    // (which carry every write so far) merge into it.
    for (int f = 0; f < N; f++) {
      g_val[f] = v[f];
      g_has[f] = has[f];
    }
    g_have_copy = true;
    g_dirty = false;
    changed |= CloudCore::sync_files(g_store);
  }
  push_if_dirty();
  if (changed & (1u << STATS)) Stats::reload();
  if (changed) g_generation++;
  return changed != 0;
}

uint32_t local_generation() { return g_generation; }

void local_written(File f) {
  if (!g_started) return;
  switch (f) {
  case SAVEGAME:
    CloudCore::save_written(g_store);
    break;
  case STATS:
    CloudCore::sync_stats(g_store, false);
    break;
  case HIGHSCORE:
    CloudCore::sync_highscore(g_store);
    break;
  }
  push_if_dirty();
}

// Every write already pushed; each resume re-reads the saved game from the
// Java side (NewtoniaActivity.onResume), so neither hook has work here.
void app_background() {}
void app_foreground() {}

}  // namespace CloudSync

#endif  // __ANDROID__ && PLAY_GAMES_BUILD
