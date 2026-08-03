// Deterministic check of the replay recorder's keyframe-ordering invariant.
//
// Why this exists (REPLAY.md, "Opening-keyframe bug + field pass"): an online
// session's recording depends on the FIRST record in the file being a
// keyframe, because every delta is encoded against one. Two windows can offer
// records before that keyframe exists — the host's first 100 ms (its opening
// keyframe rides the 10 Hz snapshot cadence, while events and effects can fire
// on any tick before it) and every client rejoin (await_keyframe re-arms the
// hold, and the next HOST keyframe is up to a second out). The recorder's
// answer is to HOLD those records rather than write them or substitute a
// locally-built keyframe; getting that wrong produced, in the field, first an
// empty host recording and then a client file that slid its whole world once
// per second.
//
// The e2e drivers never provoke either window — the game forces a keyframe at
// game start (glgame.cpp replay_start) and the host forces one for a
// (re)joining client, so in practice the first record offered IS a keyframe
// and the hold path never runs. That left the invariant untested exactly where
// it matters. This exercises it directly against the Recorder, including the
// per-seam drop count whose absence made an earlier report read "5 held" where
// 2 were.
//
// Deliberately not a test of the game's call order: it pins the recorder's
// contract, so a future caller that offers records early is degraded (a few
// records lost) rather than broken (an unplayable or drifting file).

#include "replay.h"
#include "preferences.h"

#include <SDL.h>
#include <cstdio>
#include <string>
#include <vector>

namespace Replay {

namespace {

int failures = 0;

void check(bool cond, const char *what) {
    if (cond) {
        SDL_Log("replay selftest:   ok - %s", what);
    } else {
        SDL_Log("replay selftest: FAIL - %s", what);
        failures++;
    }
}

void check_eq(int got, int want, const char *what) {
    if (got == want) {
        SDL_Log("replay selftest:   ok - %s (%d)", what, got);
    } else {
        SDL_Log("replay selftest: FAIL - %s: got %d, want %d", what, got, want);
        failures++;
    }
}

// A throwaway file beside the real replays: same directory (so this exercises
// the platform's actual replay storage, web IDBFS included) under a name the
// REPLAYS menu never lists, deleted at the end.
std::string selftest_path() {
    std::string cur = current_path();
    const std::string leaf = "current.nrp";
    if (cur.size() < leaf.size()) return "";
    return cur.substr(0, cur.size() - leaf.size()) + "selftest.nrp";
}

// Payload contents are irrelevant here — the reader walks the record framing,
// and nothing in this test decodes a snapshot. Distinct sizes make the
// records easy to tell apart in a hex dump when one of these fails.
std::vector<uint8_t> blob(size_t n, uint8_t fill) {
    return std::vector<uint8_t>(n, fill);
}

// Kinds of every intact record in the file, in order.
std::vector<uint8_t> record_kinds(const std::string &path, int *slots_ok) {
    std::vector<uint8_t> kinds;
    *slots_ok = 1;
    Reader r(path);
    if (!r.ok()) return kinds;
    Reader::Record rec;
    long long prev = -1;
    while (r.next(rec)) {
        kinds.push_back(rec.kind);
        if ((long long)rec.slot < prev) *slots_ok = 0;  // never goes backwards
        prev = (long long)rec.slot;
    }
    return kinds;
}

const char *kind_name(uint8_t k) {
    switch (k) {
        case REC_KEYFRAME: return "K";
        case REC_DELTA:    return "D";
        case REC_EVENTS:   return "E";
        case REC_EFFECT:   return "F";
        default:           return "?";
    }
}

void log_kinds(const char *label, const std::vector<uint8_t> &kinds) {
    std::string s;
    for (size_t i = 0; i < kinds.size(); i++) s += kind_name(kinds[i]);
    SDL_Log("replay selftest: %s = [%s]", label, s.c_str());
}

}  // namespace

bool selftest() {
    failures = 0;
    const std::string path = selftest_path();
    if (path.empty()) {
        SDL_Log("replay selftest: FAIL - no replay directory (pref path)");
        return false;
    }
    SDL_Log("replay selftest: %s", path.c_str());
    std::remove(path.c_str());  // a previous run's leftover would be resumed

    const uint64_t run_id = 0x5E1F7E57ULL;

    {
        Recorder r(run_id, 1, /*resumed=*/false, path);
        if (!r.ok()) {
            SDL_Log("replay selftest: FAIL - recorder would not open %s",
                    path.c_str());
            // A half-opened recorder can still have written a header; leave
            // nothing behind in the player's replays directory (the e2e
            // litter check would otherwise report a second, spurious
            // failure on top of this one).
            std::remove(path.c_str());
            return false;
        }

        // ── Seam 1: a fresh file holds everything until its keyframe ──────
        r.record_delta(blob(32, 0xD1));
        r.record_event(1, 7);
        r.record_effect(FX_SHOT, 0, blob(8, 0xF1));
        check_eq(r.predawn_drops(), 3, "fresh recorder holds delta+event+effect");
        check_eq(r.last_slot(), -1, "held records advance no slot");

        r.record_keyframe(blob(64, 0xA1));
        check_eq(r.predawn_drops(), 0, "the opening keyframe clears the hold");
        check_eq(r.last_slot(), 0, "the keyframe is slot 0");

        r.record_delta(blob(32, 0xD2));
        r.record_delta(blob(32, 0xD3));
        check_eq(r.last_slot(), 2, "records land once the baseline exists");

        // ── Seam 2: await_keyframe re-arms the hold (the client rejoin) ───
        r.await_keyframe();
        r.record_delta(blob(32, 0xD4));
        r.record_delta(blob(32, 0xD5));
        // Per-SEAM, not per-session: a cumulative count would say 5 here and
        // report drops the previous seam already accounted for.
        check_eq(r.predawn_drops(), 2, "re-armed hold counts this seam only");
        check_eq(r.last_slot(), 2, "a re-armed hold advances no slot either");

        r.record_keyframe(blob(64, 0xA2));
        check_eq(r.predawn_drops(), 0, "the seam keyframe clears the hold");
        r.record_delta(blob(32, 0xD6));

        // ended=false: an abandoned-but-resumable run, which patches the
        // header and leaves the file in place. ended=true would RETIRE it —
        // rotation and the best-of check, which would touch the player's real
        // recent.nrp/best.nrp. Not this test's business.
        r.finalize(/*score=*/1234, /*generation=*/2, /*cheated=*/false,
                   /*ended=*/false, /*player_count=*/1);
    }

    // ── The file a player would watch ────────────────────────────────────
    int slots_ok = 0;
    std::vector<uint8_t> kinds = record_kinds(path, &slots_ok);
    log_kinds("records", kinds);
    check(!kinds.empty() && kinds[0] == REC_KEYFRAME,
          "the file opens on a keyframe");
    check(slots_ok == 1, "slot indices never go backwards");
    // K D D K D — the five that landed. The five held out are absent, which
    // is the whole point: no delta in the file precedes its own baseline.
    const uint8_t want[] = {REC_KEYFRAME, REC_DELTA, REC_DELTA, REC_KEYFRAME,
                            REC_DELTA};
    check_eq((int)kinds.size(), 5, "record count");
    bool shape = kinds.size() == 5;
    for (size_t i = 0; shape && i < kinds.size(); i++)
        if (kinds[i] != want[i]) shape = false;
    check(shape, "record order is K D D K D");

    Header h;
    check(read_header(path, h), "header reads back");
    check_eq((int)h.final_score, 1234, "header score patched by finalize");
    check(h.run_id == run_id, "header keeps its run_id");

    // ── A resumed recorder must NOT hold: the file already has a keyframe ─
    {
        Recorder r(run_id, 1, /*resumed=*/true, path);
        check(r.ok(), "resumed recorder opens the existing file");
        if (r.ok()) {
            r.record_delta(blob(32, 0xD7));
            check_eq(r.predawn_drops(), 0,
                     "resumed recorder starts satisfied (no hold)");
            r.finalize(1234, 2, false, false, 1);
        }
    }
    int slots_ok2 = 0;
    std::vector<uint8_t> kinds2 = record_kinds(path, &slots_ok2);
    log_kinds("records after resume", kinds2);
    check_eq((int)kinds2.size(), 6, "the resumed delta appended");
    // The resumed recorder has to pick the slot numbering up where the file
    // left off (it reads last_slot back from the file); restarting at 0 would
    // make playback jump backwards mid-timeline.
    check(slots_ok2 == 1, "slot indices never go backwards across the resume");

    // ── Effects attach to the LAST slot, after its state record ──────────
    // Playback applies records in file order as they come due; an effect
    // stamped with the UPCOMING slot preceded that slot's state rebuild in
    // the same poll batch, and an FX_BULLET muzzle clone was destroyed
    // before a single draw. Pin the contract: an effect shares the slot of
    // the record it follows.
    {
        Recorder r(run_id, 1, /*resumed=*/true, path);
        check(r.ok(), "effect-slot recorder resumes the file");
        if (r.ok()) {
            r.record_keyframe(blob(64, 0xA3));
            r.record_effect(FX_SHOT, 0, blob(8, 0xF2));
            // At least one delta this session, or finalize's zero-tick rule
            // discards the chunk instead of writing it.
            r.record_delta(blob(32, 0xD9));
            r.finalize(1234, 2, false, false, 1);
        }
    }
    {
        Reader rd(path);
        Reader::Record rec;
        long long kf_slot = -1, fx_slot = -1;
        while (rd.next(rec)) {
            if (rec.kind == REC_KEYFRAME) kf_slot = rec.slot;
            if (rec.kind == REC_EFFECT) fx_slot = rec.slot;
        }
        check(fx_slot >= 0, "the effect landed");
        check(fx_slot >= 0 && fx_slot == kf_slot,
              "the effect carries the slot of the record it follows");
    }

    // ── A resumed recorder over a HEADER-ONLY file must hold ─────────────
    // The header is written at construction but records are RAM-only until
    // a flush, so die-once → killed process → CONTINUE legitimately resumes
    // a record-less file. Starting that seam "keyframe-satisfied" wrote
    // early effects AHEAD of the seam keyframe, and playback then rejected
    // the whole session ("no leading keyframe").
    const std::string path2 = path.substr(0, path.size() - 4) + "2.nrp";
    std::remove(path2.c_str());
    {
        Recorder fresh(run_id, 1, /*resumed=*/false, path2);
        check(fresh.ok(), "header-only file: fresh recorder opens");
        // Scope exit with no records: the header is on disk, nothing else.
    }
    {
        Recorder r(run_id, 1, /*resumed=*/true, path2);
        check(r.ok(), "header-only file: resumed recorder opens");
        if (r.ok()) {
            r.record_effect(FX_SHOT, 0, blob(8, 0xF3));
            check_eq(r.predawn_drops(), 1,
                     "header-only resume holds until the seam keyframe");
            r.record_keyframe(blob(64, 0xA4));
            r.record_delta(blob(32, 0xD8));
            r.finalize(1, 0, false, false, 1);
        }
    }
    {
        int slots_ok3 = 0;
        std::vector<uint8_t> kinds3 = record_kinds(path2, &slots_ok3);
        log_kinds("header-only resume records", kinds3);
        check(!kinds3.empty() && kinds3[0] == REC_KEYFRAME,
              "header-only resume still opens on a keyframe");
    }
    std::remove(path2.c_str());

    std::remove(path.c_str());

    // ---- run-id salt (LEADERBOARD.md S7) --------------------------------
    // The online CLIENT records under run_id ^ salt so its id is not a
    // published function of the host's. Pin the contract the derivation
    // depends on: an existing salt is returned unchanged (a value that moved
    // per call would break the rejoin resume, which re-derives the id to
    // match the file's header), and the derived id is neither the host's nor
    // the bitwise NOT the board could compute.
    //
    // Deliberately does NOT let the accessor MINT: this selftest runs before
    // load_preferences() (glut.cpp), so a mint would call save_preferences()
    // over a defaults-only g_prefs and overwrite the player's real INI.
    // Seed a known salt, exercise against it, restore.
    {
        const uint64_t SEED = 0x0123456789abcdefULL;
        uint64_t saved = g_prefs.net_run_id_salt;
        g_prefs.net_run_id_salt = SEED;
        uint64_t salt = run_id_salt();
        check(salt == SEED, "an existing salt is returned unchanged");
        check(run_id_salt() == salt, "salt is stable within a run");
        check(g_prefs.net_run_id_salt == SEED, "reading a salt never re-mints");

        const uint64_t hosts[] = { 1ULL, 0x7fffffffffffffffULL,
                                   0xffffffffffffffffULL, 0xfedcba9876543210ULL };
        for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
            uint64_t rec = hosts[i] ^ salt;
            check(rec != hosts[i], "client id differs from the host's");
            check(rec != 0, "client id is never zero");
            check(rec == (hosts[i] ^ salt), "derivation is deterministic");
            check(rec != ~hosts[i],
                  "client id is not the NOT the board could compute");
        }
        g_prefs.net_run_id_salt = saved;
    }

    SDL_Log("replay selftest: %d failure(s)", failures);
    return failures == 0;
}

}  // namespace Replay
