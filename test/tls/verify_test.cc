// Regression gate for the WebSocket TLS verification the credential-carrying
// sockets rely on (LEADERBOARD.md S1). Run via test/tls/run.sh.
//
// Deliberately .cc, not .cpp: this is a standalone binary with its own
// main(), and every build in the project sweeps game sources by *.cpp —
// the Makefile's wildcard, CMake's GLOB, CI's `find -name "*.cpp"` and
// XcodeGen's include patterns. The CI ones are RECURSIVE, so a .cpp here
// gets compiled into the game (it did: ios.yml, 2026-08-03). The find
// exclusions and the XcodeGen `test` exclude are belt and braces; the
// extension is what makes it impossible.
//
// Stands up a local TLS WebSocket server signed by a throwaway CA, then
// connects three ways through the SAME rtcWsConfiguration path the game uses
// (net_tls.h -> net_signal_rtc.cpp / net_board_rtc.cpp):
//
//   1. correct CA supplied             -> must CONNECT
//   2. unrelated CA supplied           -> must be REFUSED   <- the point
//   3. unrelated CA, verification off  -> must CONNECT      (escape hatch)
//
// Case 2 is what an on-path attacker hits when they present a certificate we
// did not sign. If it ever starts connecting, the credential a submit puts on
// the wire is readable by whoever is in the middle, and this test has done
// its job.
//
// Note this exercises libdatachannel's C API field added by
// patches/libdatachannel-ws-ca-cert.patch — a build whose dependency missed
// the patch fails to COMPILE here (and in the game), which is the loud
// failure we want.

#include <rtc/rtc.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

std::atomic<int> g_opened(0);
std::atomic<int> g_failed(0);

void RTC_API on_open(int, void *) { g_opened++; }
void RTC_API on_closed(int, void *) { g_failed++; }
void RTC_API on_error(int, const char *, void *) { g_failed++; }
void RTC_API on_client(int, int, void *) {}

// 1 = opened, 0 = refused/closed, -1 = neither inside the timeout.
int try_connect(const std::string &url, const char *ca, bool insecure) {
  g_opened = 0;
  g_failed = 0;
  rtcWsConfiguration cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.disableTlsVerification = insecure;
  if (ca) cfg.caCertificatePemFile = ca;
  int ws = rtcCreateWebSocketEx(url.c_str(), &cfg);
  if (ws < 0) return 0;
  rtcSetOpenCallback(ws, on_open);
  rtcSetClosedCallback(ws, on_closed);
  rtcSetErrorCallback(ws, on_error);
  int result = -1;
  for (int i = 0; i < 200; i++) {  // 10 s
    if (g_opened.load()) { result = 1; break; }
    if (g_failed.load()) { result = 0; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  rtcDeleteWebSocket(ws);
  return result;
}

}  // namespace

int main(int argc, char **argv) {
  // Certificate directory (run.sh mints them); default to the cwd.
  std::string dir = argc > 1 ? argv[1] : ".";
  if (!dir.empty() && dir[dir.size() - 1] != '/') dir += '/';
  const std::string ca = dir + "ca.pem";
  const std::string other = dir + "other-ca.pem";
  const std::string crt = dir + "srv.crt";
  const std::string key = dir + "srv.key";

  int server = -1, port = 0;
  for (int p = 19443; p < 19453 && server < 0; p++) {
    rtcWsServerConfiguration sc;
    memset(&sc, 0, sizeof(sc));
    sc.port = (uint16_t)p;
    sc.enableTls = true;
    sc.certificatePemFile = crt.c_str();
    sc.keyPemFile = key.c_str();
    // Bind v4 explicitly: the default listener is v6 with a dual-stack
    // mapping, and a container without IPv6 fails socket creation outright.
    sc.bindAddress = "127.0.0.1";
    server = rtcCreateWebSocketServer(&sc, on_client);
    if (server >= 0) port = p;
  }
  if (server < 0) {
    printf("FAIL: could not start the local TLS server\n");
    return 1;
  }
  const std::string url = "wss://localhost:" + std::to_string(port) + "/";
  printf("tls: server on %s\n", url.c_str());

  const struct {
    const char *name;
    const std::string &ca;
    bool insecure;
    int want;
  } cases[] = {
      {"correct CA",              ca,    false, 1},
      {"unrelated CA",            other, false, 0},
      {"unrelated CA + insecure", other, true,  1},
  };

  int bad = 0;
  for (const auto &c : cases) {
    int got = try_connect(url, c.ca.c_str(), c.insecure);
    const char *word = got == 1 ? "connected" : got == 0 ? "refused" : "timeout";
    const bool ok = got == c.want;
    printf("tls:   %-24s -> %-9s  %s\n", c.name, word, ok ? "ok" : "WRONG");
    if (!ok) bad++;
  }
  rtcDeleteWebSocketServer(server);
  printf(bad ? "tls: FAIL\n" : "tls: PASS\n");
  return bad ? 1 : 0;
}
