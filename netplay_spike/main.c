/* Phase-0 spike: prove the libdatachannel C API does everything the game's
 * NetTransport backend needs, in-process loopback:
 *   - two peer connections, offer/answer + candidate exchange
 *   - a reliable-ordered channel and an unreliable-unordered channel
 *     (validates the rtcDataChannelInit/rtcReliability struct layout)
 *   - binary message round-trip on both channels
 * Prints SPIKE PASS / SPIKE FAIL. */
#include <rtc/rtc.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int pc1 = -1, pc2 = -1;      /* pc1 = "host" side */
static volatile LONG rel_echoed = 0, unrel_echoed = 0;
static volatile LONG open_count = 0;

/* --- signaling shims: hand descriptions/candidates straight to the peer --- */
static void RTC_API on_desc1(int pc, const char *sdp, const char *type, void *p) {
    (void)pc; (void)p; rtcSetRemoteDescription(pc2, sdp, type);
}
static void RTC_API on_desc2(int pc, const char *sdp, const char *type, void *p) {
    (void)pc; (void)p; rtcSetRemoteDescription(pc1, sdp, type);
}
static void RTC_API on_cand1(int pc, const char *cand, const char *mid, void *p) {
    (void)pc; (void)p; rtcAddRemoteCandidate(pc2, cand, mid);
}
static void RTC_API on_cand2(int pc, const char *cand, const char *mid, void *p) {
    (void)pc; (void)p; rtcAddRemoteCandidate(pc1, cand, mid);
}

/* --- pc2 side: echo whatever arrives back on the same channel --- */
static void RTC_API on_message_echo(int dc, const char *msg, int size, void *p) {
    (void)p;
    if (size > 0) rtcSendMessage(dc, msg, size); /* binary echo */
}
static void RTC_API on_channel2(int pc, int dc, void *p) {
    (void)pc; (void)p;
    rtcSetMessageCallback(dc, on_message_echo);
}

/* --- pc1 side: mark pass when our probe comes back --- */
static void RTC_API on_message_back_rel(int dc, const char *msg, int size, void *p) {
    (void)dc; (void)p;
    if (size == 4 && memcmp(msg, "REL!", 4) == 0) InterlockedExchange(&rel_echoed, 1);
}
static void RTC_API on_message_back_unrel(int dc, const char *msg, int size, void *p) {
    (void)dc; (void)p;
    if (size == 4 && memcmp(msg, "UNR!", 4) == 0) InterlockedExchange(&unrel_echoed, 1);
}
static void RTC_API on_open(int dc, void *p) {
    (void)dc; (void)p; InterlockedIncrement(&open_count);
}

int main(void) {
    rtcInitLogger(RTC_LOG_WARNING, NULL);

    rtcConfiguration cfg;
    memset(&cfg, 0, sizeof(cfg)); /* no STUN needed for loopback */

    pc1 = rtcCreatePeerConnection(&cfg);
    pc2 = rtcCreatePeerConnection(&cfg);
    if (pc1 < 0 || pc2 < 0) { printf("SPIKE FAIL: create pc\n"); return 1; }

    rtcSetLocalDescriptionCallback(pc1, on_desc1);
    rtcSetLocalDescriptionCallback(pc2, on_desc2);
    rtcSetLocalCandidateCallback(pc1, on_cand1);
    rtcSetLocalCandidateCallback(pc2, on_cand2);
    rtcSetDataChannelCallback(pc2, on_channel2);

    /* reliable ordered channel (all defaults) */
    int rel = rtcCreateDataChannel(pc1, "rel");
    if (rel < 0) { printf("SPIKE FAIL: create rel dc\n"); return 1; }
    rtcSetOpenCallback(rel, on_open);
    rtcSetMessageCallback(rel, on_message_back_rel);

    /* unreliable unordered channel — this validates the init struct we rely on */
    rtcDataChannelInit init;
    memset(&init, 0, sizeof(init));
    init.reliability.unordered = true;
    init.reliability.maxRetransmits = 0;
    init.reliability.unreliable = true;
    int unrel = rtcCreateDataChannelEx(pc1, "unrel", &init);
    if (unrel < 0) { printf("SPIKE FAIL: create unrel dc\n"); return 1; }
    rtcSetOpenCallback(unrel, on_open);
    rtcSetMessageCallback(unrel, on_message_back_unrel);

    /* wait for both channels to open */
    int i;
    for (i = 0; i < 100 && open_count < 2; ++i) Sleep(100);
    if (open_count < 2) { printf("SPIKE FAIL: channels did not open (%ld)\n", open_count); return 1; }

    /* round-trip probes; resend unreliable a few times (loopback shouldn't drop, but be safe) */
    for (i = 0; i < 50 && !(rel_echoed && unrel_echoed); ++i) {
        if (!rel_echoed)   rtcSendMessage(rel,   "REL!", 4);
        if (!unrel_echoed) rtcSendMessage(unrel, "UNR!", 4);
        Sleep(100);
    }

    printf(rel_echoed && unrel_echoed ? "SPIKE PASS\n"
                                      : "SPIKE FAIL: rel=%ld unrel=%ld\n",
           rel_echoed, unrel_echoed);

    rtcDeletePeerConnection(pc1);
    rtcDeletePeerConnection(pc2);
    rtcCleanup();
    return rel_echoed && unrel_echoed ? 0 : 1;
}
