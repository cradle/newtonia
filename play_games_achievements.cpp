// Google Play Games Services achievements backend (ACHIEVEMENTS.md §2) —
// compiled only when PLAY_GAMES_BUILD is defined, which the root
// CMakeLists.txt sets for every Android build (the only build that uses that
// CMake file). Every other build compiles this file to an empty translation
// unit via the guard below.
//
// The Play Games v2 SDK is Java-only, so this file is the native half of a
// two-part backend: it maps symbolic IDs (§5 master list) to Android string
// resource names and forwards earns over JNI to
// android/app/src/main/java/org/newtonia/PlayGamesAchievements.java, which
// owns the SDK calls (PlayGamesSdk.initialize, automatic sign-in,
// AchievementsClient.unlock/setSteps). Division of labour:
// - This side: the authoritative symbolic→resource-name table (coop_clear
//   deliberately absent until netplay — same decision as Game Center), an
//   in-memory best-sent cache so the seam's idempotent re-fires (thresholds
//   re-unlock every generation rebuild) don't spam JNI + UI-thread posts,
//   and progress filtering (only incremental achievements report percent —
//   mirrors the Steam backend's stat gating).
// - Java side: resolves the resource name to the Play Console-generated ID
//   (games-ids.xml), marshals to the UI thread, queues earns in memory until
//   the SDK's automatic sign-in resolves, and drops unmapped earns with a
//   one-shot log. Play services caches unlock/setSteps client-side and syncs
//   when connectivity returns, so no pending journal is needed here
//   (ACHIEVEMENTS.md §2 "Offline earns") — only the pre-sign-in window is
//   bridged, by the Java queue.
//
// Threading: unlock/progress are called on the game thread (SDL_main's
// thread), whose JNIEnv comes from SDL_AndroidGetJNIEnv(); the Java side
// immediately posts to the UI thread, where all its state lives.

#if defined(__ANDROID__) && defined(PLAY_GAMES_BUILD)

#include <jni.h>
#include <SDL.h>

#include <cstring>
#include <iostream>
#include <map>
#include <string>

namespace {

// Symbolic ID (ACHIEVEMENTS.md §5) → games-ids.xml string resource name,
// plus whether the Play Console definition is incremental (100 steps, so the
// seam's percent maps 1:1 to steps — the same seven counter-backed
// achievements that get progress stats on Steam). This table is
// authoritative: the games-ids.xml entries must use exactly these resource
// names (§2 Play Games checklist), whatever ID values the console generates.
// coop_clear is deliberately absent: touch builds have no proven local-2P
// path, so its definition and mapping wait for netplay (unmapped earns drop
// silently and re-fire in a future co-op game once the mapping exists).
struct Mapping {
  const char *symbolic;
  const char *res;   // string resource name holding the Play Console ID
  bool incremental;  // defined in the console as incremental, 100 steps
};
const Mapping MAPPINGS[] = {
  { "first_kill",           "achievement_first_kill",           false },
  { "clear_level1",         "achievement_clear_level1",         false },
  { "specials_7",           "achievement_specials_7",           true  },
  { "black_hole_survivor",  "achievement_black_hole_survivor",  false },
  { "mini_station_kill",    "achievement_mini_station_kill",    false },
  { "shield_ram",           "achievement_shield_ram",           false },
  { "shield_ram_asteroid",  "achievement_shield_ram_asteroid",  false },
  { "station_destroyed",    "achievement_station_destroyed",    false },
  { "enemies_10",           "achievement_enemies_10",           true  },
  { "nova_detonated",       "achievement_nova_detonated",       false },
  { "no_damage_clear",      "achievement_no_damage_clear",      false },
  { "no_secondary_level10", "achievement_no_secondary_level10", false },
  { "weapons_7",            "achievement_weapons_7",            true  },
  { "kills_1000",           "achievement_kills_1000",           true  },
  { "kills_10000_lifetime", "achievement_kills_10000_lifetime", true  },
  { "score_3m",             "achievement_score_3m",             true  },
  { "reach_level15",        "achievement_reach_level15",        true  },
};

const Mapping *find_mapping(const char *symbolic) {
  for (const Mapping &m : MAPPINGS)
    if (std::strcmp(m.symbolic, symbolic) == 0) return &m;
  return NULL;  // unknown symbol: drop silently (new ID without a mapping yet)
}

jclass g_bridge = NULL;       // global ref to PlayGamesAchievements
jmethodID g_init = NULL;      // static void init(Activity)
jmethodID g_report = NULL;    // static void report(String, int, boolean)

// Best percent already handed to the Java side, per symbolic ID. The seam
// re-fires unlocks constantly; without this every rebuild would cost a JNI
// call and a UI-thread post per threshold achievement. The Java side keeps
// its own pre-sign-in queue, so suppressing an exact re-send here never
// loses an earn.
std::map<std::string, int> g_sent;

// Log-and-clear any pending Java exception so a bridge failure can never
// take the game thread down. Returns true if one was pending.
bool clear_exception(JNIEnv *env) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionDescribe();  // goes to logcat
  env->ExceptionClear();
  return true;
}

// Resolve the Java bridge class. FindClass normally works from the game
// thread (SDL_main runs beneath a Java frame, so the app class loader is in
// scope), but fall back to the activity's own class loader if it doesn't.
jclass resolve_bridge_class(JNIEnv *env, jobject activity) {
  jclass cls = env->FindClass("org/newtonia/PlayGamesAchievements");
  if (cls && !clear_exception(env)) return cls;
  clear_exception(env);
  // Check after every step: a JNI call made with an exception pending (or a
  // null object) is undefined behaviour, and CheckJNI — on for every
  // debuggable APK — turns it into an abort. None of these lookups should
  // fail, but this path exists precisely for the unexpected case.
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
    name = env->NewStringUTF("org.newtonia.PlayGamesAchievements");
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

void call_report(const Mapping *m, int pct) {
  if (!g_bridge) return;  // init failed: achievements off this session
  int &best = g_sent[m->symbolic];
  if (best >= pct) return;
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  if (!env) return;
  jstring res = env->NewStringUTF(m->res);
  // NewStringUTF fails only under OOM, but it fails with an exception
  // PENDING — calling any other JNI function then is undefined behaviour
  // (CheckJNI aborts), and a null String would NPE on the Java UI thread.
  if (!res || clear_exception(env)) return;
  env->CallStaticVoidMethod(g_bridge, g_report, res, (jint)pct,
                            (jboolean)(m->incremental ? JNI_TRUE : JNI_FALSE));
  if (!clear_exception(env)) best = pct;
  env->DeleteLocalRef(res);
}

} // namespace

namespace Achievements {
namespace Backend {

// Called from Achievements::init() in android_main.cpp, after SDL_Init (the
// activity handle must exist) and before the first frame. Caches the JNI
// bridge and hands the activity to the Java side, which initialises the
// Play Games SDK and kicks off its automatic sign-in. Safe to run again if
// SDL_main re-enters in a kept-alive process — the Java side just refreshes
// its activity reference.
void init() {
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  jobject activity = (jobject)SDL_AndroidGetActivity();
  if (!env || !activity) {
    std::cout << "Play Games init: no JNI activity — achievements disabled"
              << std::endl;
    return;
  }
  // Re-entry (kept-alive process, SDL_main runs again): drop the best-sent
  // cache. The previous session's Java side may have dropped everything in
  // it (e.g. Play services was being updated and the SDK never initialised),
  // and re-sends are harmless — unlock is idempotent, setSteps monotonic.
  g_sent.clear();
  if (!g_bridge) {
    jclass cls = resolve_bridge_class(env, activity);
    if (cls) {
      g_init = env->GetStaticMethodID(cls, "init", "(Landroid/app/Activity;)V");
      // A failed lookup leaves its exception PENDING; clear it before the
      // next JNI call or that call is undefined behaviour (CheckJNI abort) —
      // this graceful-degradation path must itself not crash.
      clear_exception(env);
      g_report = env->GetStaticMethodID(cls, "report",
                                        "(Ljava/lang/String;IZ)V");
      if (clear_exception(env) || !g_init || !g_report) {
        g_init = g_report = NULL;
      } else {
        g_bridge = (jclass)env->NewGlobalRef(cls);
      }
      env->DeleteLocalRef(cls);
    }
    if (!g_bridge)
      std::cout << "Play Games bridge unavailable — achievements disabled"
                << std::endl;
  }
  if (g_bridge) {
    env->CallStaticVoidMethod(g_bridge, g_init, activity);
    clear_exception(env);
  }
  env->DeleteLocalRef(activity);
}

void unlock(const char *id) {
  const Mapping *m = find_mapping(id);
  if (!m) return;
  call_report(m, 100);
}

void progress(const char *id, int pct) {
  const Mapping *m = find_mapping(id);
  if (!m || !m->incremental) return;  // event-only: nothing to report
  // Range already enforced by the shared seam (achievements.cpp).
  call_report(m, pct);
}

} // namespace Backend
} // namespace Achievements

#endif // __ANDROID__ && PLAY_GAMES_BUILD
