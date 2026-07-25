#include "net_resume.h"

#include <SDL.h>

#include <cstdio>
#include <ctime>

#include "net_protocol.h"
#include "savegame.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

const char *NR_ORG  = "cc.gfm";
const char *NR_APP  = "newtonia";
const char *NR_FILE = "netplay_resume.dat";

std::string ticket_path() {
  char *dir = SDL_GetPrefPath(NR_ORG, NR_APP);
  if (!dir) return "";
  std::string path = std::string(dir) + NR_FILE;
  SDL_free(dir);
  return path;
}

long long self_pid() {
#if defined(__EMSCRIPTEN__)
  return 0;  // one instance per origin; liveness is moot on web
#elif defined(_WIN32)
  return (long long)GetCurrentProcessId();
#else
  return (long long)getpid();
#endif
}

// Is the ticket's writer still running? A live writer means the room is
// HOSTED right now — a sibling instance sharing this pref dir (loopback
// testing, Steam Deck + desktop) must not be offered a resume that would
// reclaim the room out from under it. False negatives are impossible
// (the writer refreshes the ticket while alive); a recycled pid can fake
// "alive" for up to the grace window — the row goes missing, nothing
// corrupts.
bool process_alive(long long pid) {
  if (pid <= 0) return false;
#if defined(__EMSCRIPTEN__)
  return false;
#elif defined(_WIN32)
  HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
  if (!h) return false;
  DWORD wait = WaitForSingleObject(h, 0);
  CloseHandle(h);
  return wait == WAIT_TIMEOUT;  // signaled = exited
#else
  return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

void idbfs_sync() {
#ifdef __EMSCRIPTEN__
  EM_ASM(
      FS.syncfs(false, function(err) {
        if (err) console.error('[newtonia] IDBFS resume-ticket sync failed:', err);
      });
  );
#endif
}

}  // namespace

namespace NetResume {

const long long GRACE_MS = 2 * 60 * 1000;

bool write(const std::string &room_code, const std::string &room_token) {
  std::string path = ticket_path();
  if (path.empty() || room_code.empty() || room_token.empty()) return false;
  FILE *fp = fopen(path.c_str(), "w");
  if (!fp) return false;
  bool ok = fprintf(fp, "NWRS 1 %u\n%s\n%s\n%lld\n%lld\n",
                    (unsigned)Net::PROTO_VERSION, room_code.c_str(),
                    room_token.c_str(), (long long)time(NULL),
                    self_pid()) > 0;
  fclose(fp);
  idbfs_sync();
  return ok;
}

bool read(std::string &room_code, std::string &room_token,
          long long &age_ms) {
  std::string path = ticket_path();
  if (path.empty()) return false;
  FILE *fp = fopen(path.c_str(), "r");
  if (!fp) return false;
  char magic[8] = {0};
  unsigned version = 0, proto = 0;
  char code[64] = {0}, token[256] = {0};
  long long ts = 0, pid = 0;
  bool ok = fscanf(fp, "%7s %u %u %63s %255s %lld %lld",
                   magic, &version, &proto, code, token, &ts, &pid) == 7 &&
            std::string(magic) == "NWRS" && version == 1 &&
            proto == (unsigned)Net::PROTO_VERSION && ts > 0;
  fclose(fp);
  if (!ok) return false;
  // The writer still runs: the room is live-hosted by a sibling instance
  // sharing this pref dir, not resumable — and its ticket is not ours to
  // delete, so this is a plain "no", never a cleanup.
  if (pid != self_pid() && process_alive(pid)) return false;
  room_code = code;
  room_token = token;
  long long now = (long long)time(NULL);
  age_ms = now >= ts ? (now - ts) * 1000 : 0;
  return true;
}

void clear() {
  std::string path = ticket_path();
  if (path.empty()) return;
  std::remove(path.c_str());
  idbfs_sync();
}

void clear_with_save() {
  clear();
  Save::delete_online_save();
}

}  // namespace NetResume
