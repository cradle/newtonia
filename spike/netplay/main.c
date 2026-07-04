/* Phase 0 netplay spike — see NETPLAY.md and CMakeLists.txt alongside.
 *
 * In-process two-peer-connection loopback over the libdatachannel C API
 * (rtc/rtc.h). Exercises exactly what the real backend (Phase 2) needs:
 *
 *   - two channels from one side: "rel" (default ordered/reliable) and
 *     "unrel" created with rtcCreateDataChannelEx + rtcReliability
 *     { unordered, unreliable, maxRetransmits = 0 } — the INPUT channel
 *     configuration, and the struct whose shape this spike validates;
 *   - callbacks firing on library worker threads while the main thread
 *     polls flags (the real backend's mutex/deque pattern in miniature);
 *   - a binary echo round-trip on both channels.
 *
 * Prints "SPIKE PASS" and exits 0 on success; "SPIKE FAIL" and exits 1
 * on timeout or error. Descriptions/candidates are cross-wired directly
 * (in-process trickle) — signaling UX is not what phase 0 de-risks.
 */

#include <rtc/rtc.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
#else
#include <unistd.h>
static void sleep_ms(int ms) { usleep(ms * 1000); }
#endif

/* Plain volatile ints are enough for a spike: flag stores are word-sized
 * and the main thread only polls until they flip. */
static volatile int g_rel_echo_ok = 0;
static volatile int g_unrel_echo_ok = 0;
static volatile int g_failed = 0;

static int g_pc1 = 0, g_pc2 = 0;
static int g_dc_rel = 0, g_dc_unrel = 0;
static volatile int g_dc_unrel_open = 0;

static const char REL_PAYLOAD[] = { 1, 2, 3, 4, 5 };
static const char UNREL_PAYLOAD[] = { 9, 8, 7 };

/* --- cross-wiring: each PC's local description/candidate goes straight
 *     to the other PC ------------------------------------------------- */

static void on_local_description(int pc, const char *sdp, const char *type,
                                 void *ptr) {
  int other = (pc == g_pc1) ? g_pc2 : g_pc1;
  printf("[pc%d] local description (%s)\n", pc == g_pc1 ? 1 : 2, type);
  fflush(stdout);
  if (rtcSetRemoteDescription(other, sdp, type) < 0) {
    printf("rtcSetRemoteDescription failed\n");
    g_failed = 1;
  }
  (void)ptr;
}

static void on_local_candidate(int pc, const char *cand, const char *mid,
                               void *ptr) {
  int other = (pc == g_pc1) ? g_pc2 : g_pc1;
  if (rtcAddRemoteCandidate(other, cand, mid) < 0) {
    printf("rtcAddRemoteCandidate failed\n");
    g_failed = 1;
  }
  (void)ptr;
}

static void on_state_change(int pc, rtcState state, void *ptr) {
  printf("[pc%d] state %d\n", pc == g_pc1 ? 1 : 2, (int)state);
  fflush(stdout);
  if (state == RTC_FAILED)
    g_failed = 1;
  (void)ptr;
}

/* --- pc2: echo every message back on the channel it arrived on ------- */

static void on_pc2_message(int dc, const char *message, int size, void *ptr) {
  rtcSendMessage(dc, message, size); /* negative size = string, echo as-is */
  (void)ptr;
}

static void on_pc2_data_channel(int pc, int dc, void *ptr) {
  char label[64] = "?";
  rtcGetDataChannelLabel(dc, label, (int)sizeof(label));
  printf("[pc2] incoming channel \"%s\"\n", label);
  fflush(stdout);
  rtcSetMessageCallback(dc, on_pc2_message);
  (void)pc;
  (void)ptr;
}

/* --- pc1: send the test payloads on open, verify the echoes ---------- */

static void on_rel_open(int dc, void *ptr) {
  printf("[pc1] \"rel\" open\n");
  fflush(stdout);
  rtcSendMessage(dc, REL_PAYLOAD, (int)sizeof(REL_PAYLOAD));
  (void)ptr;
}

static void on_unrel_open(int dc, void *ptr) {
  printf("[pc1] \"unrel\" open\n");
  fflush(stdout);
  g_dc_unrel_open = 1; /* main loop (re)sends — unreliable may drop */
  (void)dc;
  (void)ptr;
}

static void on_pc1_message(int dc, const char *message, int size, void *ptr) {
  if (dc == g_dc_rel && size == (int)sizeof(REL_PAYLOAD) &&
      memcmp(message, REL_PAYLOAD, sizeof(REL_PAYLOAD)) == 0)
    g_rel_echo_ok = 1;
  else if (dc == g_dc_unrel && size == (int)sizeof(UNREL_PAYLOAD) &&
           memcmp(message, UNREL_PAYLOAD, sizeof(UNREL_PAYLOAD)) == 0)
    g_unrel_echo_ok = 1;
  else {
    printf("unexpected message on dc %d (size %d)\n", dc, size);
    g_failed = 1;
  }
  (void)ptr;
}

int main(void) {
  rtcInitLogger(RTC_LOG_WARNING, NULL);

  rtcConfiguration config;
  memset(&config, 0, sizeof(config)); /* no STUN: loopback host candidates */

  g_pc1 = rtcCreatePeerConnection(&config);
  g_pc2 = rtcCreatePeerConnection(&config);
  if (g_pc1 < 0 || g_pc2 < 0) {
    printf("rtcCreatePeerConnection failed\nSPIKE FAIL\n");
    return 1;
  }

  rtcSetLocalDescriptionCallback(g_pc1, on_local_description);
  rtcSetLocalDescriptionCallback(g_pc2, on_local_description);
  rtcSetLocalCandidateCallback(g_pc1, on_local_candidate);
  rtcSetLocalCandidateCallback(g_pc2, on_local_candidate);
  rtcSetStateChangeCallback(g_pc1, on_state_change);
  rtcSetStateChangeCallback(g_pc2, on_state_change);
  rtcSetDataChannelCallback(g_pc2, on_pc2_data_channel);

  /* "rel": defaults (ordered, reliable) — snapshots/handshake/events. */
  g_dc_rel = rtcCreateDataChannel(g_pc1, "rel");

  /* "unrel": the INPUT channel config. This struct usage is one of the
   * things the spike exists to validate against the pinned version. */
  rtcDataChannelInit init;
  memset(&init, 0, sizeof(init));
  init.reliability.unordered = true;
  init.reliability.unreliable = true;
  init.reliability.maxRetransmits = 0;
  g_dc_unrel = rtcCreateDataChannelEx(g_pc1, "unrel", &init);

  if (g_dc_rel < 0 || g_dc_unrel < 0) {
    printf("data channel creation failed (rel=%d unrel=%d)\nSPIKE FAIL\n",
           g_dc_rel, g_dc_unrel);
    return 1;
  }

  rtcSetOpenCallback(g_dc_rel, on_rel_open);
  rtcSetOpenCallback(g_dc_unrel, on_unrel_open);
  rtcSetMessageCallback(g_dc_rel, on_pc1_message);
  rtcSetMessageCallback(g_dc_unrel, on_pc1_message);

  /* Poll up to 30 s; re-send the unreliable payload each second since
   * maxRetransmits=0 gives no delivery guarantee even on loopback. */
  int waited_ms = 0;
  while (waited_ms < 30000 && !g_failed &&
         !(g_rel_echo_ok && g_unrel_echo_ok)) {
    sleep_ms(100);
    waited_ms += 100;
    if (g_dc_unrel_open && !g_unrel_echo_ok && waited_ms % 1000 == 0)
      rtcSendMessage(g_dc_unrel, UNREL_PAYLOAD, (int)sizeof(UNREL_PAYLOAD));
  }

  int pass = g_rel_echo_ok && g_unrel_echo_ok && !g_failed;
  printf("rel echo: %s, unrel echo: %s, failed flag: %d\n",
         g_rel_echo_ok ? "ok" : "MISSING",
         g_unrel_echo_ok ? "ok" : "MISSING", (int)g_failed);

  rtcDeleteDataChannel(g_dc_rel);
  rtcDeleteDataChannel(g_dc_unrel);
  rtcDeletePeerConnection(g_pc1);
  rtcDeletePeerConnection(g_pc2);
  rtcCleanup();

  printf(pass ? "SPIKE PASS\n" : "SPIKE FAIL\n");
  return pass ? 0 : 1;
}
