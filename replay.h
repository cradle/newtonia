#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Replay recording (REPLAY.md R1). A replay file is the netplay host's
// snapshot feed written to disk: KEYFRAME/DELTA records in exactly the wire
// encodings (built by the same GLGame builders net_host_send_snapshot uses)
// plus EVENTS records teed from net_send_event. Playback (R2) is the net
// client's apply path fed from the file.
//
// Storage model (REPLAY.md): records since the last flush accumulate in an
// in-RAM chunk; each checkpoint APPENDS the chunk to the recording file —
// never a whole-file rewrite — and the header's tail fields are patched in
// place only at run end / clean abandon. Four files, all watchable (the
// REPLAYS menu lists one row per existing file):
//   current.nrp  the active (live or resumable) OFFLINE run
//   recent.nrp   the most recently completed offline run (current rotates
//                here at game over / NEW-GAME abandonment)
//   online.nrp   the most recent ONLINE session (both roles record — the
//                host tees the snapshots it builds, the client the stream
//                it receives). This file never rotates: it IS the listed
//                ONLINE RUN slot, overwritten by the next online session
//                like recent is by the next offline run — and it is
//                deliberately separate from current.nrp so hosting/joining
//                mid-way through an offline run never rotates that run's
//                resumable recording away
//   best.nrp     promoted copy of the highest-scoring non-cheated run,
//                offline or online (checked at each retirement)
//
// File layout (little-endian native, like the savegame):
//   header (HEADER_SIZE bytes, see Header)
//   records: [u32 slot | u8 kind | u32 len | payload] ...
// Records are self-delimiting: a reader drops a truncated final record (a
// crash artifact) and must tolerate a header staler than the records behind
// it (appends land between header patches).

namespace Replay {

// Compile-time game version stamped into headers ("dev" unless the build
// system defines it). Informational only; compatibility runs on format_version.
#ifndef NEWTONIA_VERSION_STRING
#define NEWTONIA_VERSION_STRING "dev"
#endif

struct Header {
    static const uint32_t MAGIC = 0x5052574Eu;  // "NWRP" (little-endian)
    // v2 adds FX_BULLET (see EffectSubtype). Purely additive: a v1 file
    // simply carries none, and playback falls back to the snapshot-rebuilt
    // bullets it always used — so v1 files keep loading and behaving as
    // they do today.
    static const uint16_t FORMAT_VERSION = 2;
    // Oldest format this build still reads. Raise this, not the check,
    // when support for a format is actually dropped.
    static const uint16_t MIN_FORMAT_VERSION = 1;
    static const size_t   SIZE = 64;            // v1 header size on disk
    static const size_t   GAME_VERSION_LEN = 24;
    // Patchable tail: flags..duration_ms live at bytes [PATCH_OFFSET, SIZE).
    static const size_t   PATCH_OFFSET = 48;

    uint16_t format_version = FORMAT_VERSION;
    uint16_t header_size = (uint16_t)SIZE;  // where records start
    char     game_version[GAME_VERSION_LEN] = {0};
    uint64_t run_id = 0;      // matches GameState::run_id (immutable)
    uint64_t date = 0;        // epoch seconds at creation (immutable)
    // -- patchable tail (stale between patches; by design) --
    uint8_t  flags = 0;       // Flags below
    uint8_t  player_count = 1;
    // Savegame format the record payloads were serialized with
    // (GameState::VERSION at record time) — playback passes it to
    // deserialize_game so a newer build can still parse an older file's
    // snapshot bytes. 0 (pre-field files) = assume the current build's.
    uint16_t save_version = 0;
    uint32_t final_score = 0;
    uint32_t generation = 0;  // generation reached
    uint32_t duration_ms = 0; // timeline ms (slot count x 100 at last patch)
};

enum Flags {
    FLAG_CHEATED = 1,  // cheat-flagged run: can never become best.nrp
    FLAG_CLEAN   = 2,  // header patched at a clean stop — tail is accurate
    FLAG_ENDED   = 4,  // run truly over (game over), not merely abandoned
};

enum RecordKind : uint8_t {
    REC_KEYFRAME = 1,  // full snapshot payload (net keyframe encoding)
    REC_DELTA    = 2,  // delta payload (net delta encoding)
    REC_EVENTS   = 3,  // one EV_* event: u8 code | u32 arg
    // Transient weapon visual (u8 subtype | u8 player index | body).
    // Snapshots carry projectiles but not the flash-class visuals — online
    // those ride MSG_LANCE/MSG_SHOCK echoes or are host-local (nova/giga
    // rings), none of which a solo game emits. LANCE/SHOCK bodies use the
    // exact MSG wire encodings and play back through the same receive
    // functions the net client uses.
    REC_EFFECT   = 4,
};

enum EffectSubtype : uint8_t {
    FX_LANCE = 1,  // MSG_LANCE body: u8 count | count * (f32 x, f32 y)
    FX_SHOCK = 2,  // MSG_SHOCK body: u8 count | count * (f32 x, f32 y)
    FX_RING  = 3,  // shockwave ring: f32 x,y,max_r,speed,duration | u8 nova
    FX_SHOT  = 4,  // gun-shot sound cue: f32 x, f32 y | u8 kind (0 pew, 1 beam)
                   // (the SOUND rode MSG_SHOT/EV_WORLD_SHOT). One per trigger
                   // pull, from ANY ship — enemies and the mini-station too.
    // v2: one per spawned BULLET, on the owning player (the record's idx).
    // f32 x, f32 y, f32 vx, f32 vy | u8 flags (1 kills_invincible, 2 trail,
    // 4 piercing) — net_spawn_reported_bullet's argument order.
    //
    // Playback spawns an exact clone from this, which is the whole point:
    // a bullet used to reach playback ONLY via the 10 Hz snapshot rebuild,
    // so it popped into view up to a snapshot interval late and already
    // down-range, on a heading that no longer matched the nose it left.
    // PROTO 17 fixed the identical complaint online by echoing MSG_SHOT so
    // the client spawns clones instantly; this is that fix for the file.
    // The next apply replaces the clone with authority's copy, exactly as
    // the online client's snapshot rebuild does.
    FX_BULLET = 5,
};

// Paths in <pref>/replays/ (created on demand). Empty string on failure.
std::string current_path();
std::string recent_path();
std::string best_path();
std::string online_path();

uint64_t new_run_id();  // never returns 0

// Whether an environment override is forcing recording on or off, and which
// way: -1 none, 0 forced OFF, 1 forced ON (DISABLE wins over ENABLE). ONE
// definition, shared by the recorder gate (GLGame::replay_start) and the
// Options row, which appends "ENV ON"/"ENV OFF" while one is active — a row
// reading OFF while NEWTONIA_REPLAY_ENABLE recorded anyway cost a real
// debugging session (field, 2026-07-28: the var rode an adb intent extra
// and outlived every relaunch that reused the process). The row still
// shows and edits the STORED preference: a control that silently ignores
// input is no clearer than one that lies, so the marker carries the
// warning instead of the value doing it.
int recording_override();

// Why a file's header would not load. The replays list shows these to the
// player, so they are kept apart rather than collapsed into one "can't read
// it": a file from a newer build tells the player something they can act on
// (update the game), while a damaged one does not, and calling either of
// them "older" would simply be wrong.
enum HeaderStatus {
    HEADER_OK = 0,
    // Missing, too short, wrong magic, or a nonsense header_size/version 0.
    // Damage or not a replay at all — nothing to do with format versions.
    HEADER_DAMAGED,
    // A format version this build no longer reads. Still unreachable — the
    // format is at v2 but MIN_FORMAT_VERSION is 1, so both are accepted;
    // this becomes real the day support for v1 is actually dropped.
    HEADER_TOO_OLD,
    // Recorded by a newer build than this one.
    HEADER_TOO_NEW,
};

// Reads+validates a header (magic, format version, sane header_size) and
// says which of the above it is. `h` is only filled on HEADER_OK.
HeaderStatus read_header_status(const std::string &path, Header &h);

// Reads+validates a header (magic, format version, sane header_size).
// Shorthand for read_header_status(...) == HEADER_OK, for the callers that
// only need "can I play this".
bool read_header(const std::string &path, Header &h);

// True if the file contains at least one DELTA record — a record-less
// current.nrp (instant quit) is junk and is never rotated into recent.
bool has_delta_record(const std::string &path);

// Highest slot index among the file's intact records (-1 if none). A
// resumed recording continues numbering from here so the timeline stays
// continuous across exit→continue.
int last_record_slot(const std::string &path);

// Rotate current → recent, promoting a copy to best first when eligible
// (header CLEAN — accurate — and not cheat-flagged, score beats best's).
// A current with no DELTA records is deleted instead. Safe no-op when no
// current exists. Called at run end (via Recorder::finalize) and by
// on_new_game() for a run being abandoned forever.
void rotate_current_to_recent();

// NEW GAME over a leftover current.nrp (a previous run being discarded,
// possibly a crash artifact): rotate it into recent so it isn't silently
// lost, then the caller starts a fresh Recorder.
void on_new_game();

// Run the best check on online.nrp in place (clean, non-cheated header
// beating best's score → copy promoted). online.nrp never rotates — it is
// the listed ONLINE RUN slot — so this is the whole of its retirement:
// called from finalize when an online run truly ends, and from the start
// of a new online recording that is about to overwrite a cleanly-closed
// leftover (the twin of the NEW-GAME rotation's best check offline). Safe
// no-op when the file is missing, junk, or stale-headered (a crashed
// session stays watchable in its slot but can't become best — the same
// accepted limitation as offline).
void best_check_online();

// Playback-side file reader (R2). Loads the whole file into memory (a few
// MB) and iterates intact records in order; a truncated final record (crash
// artifact) simply ends iteration, and the header may understate the
// records behind it (both legal — see the storage model in REPLAY.md).
class Reader {
public:
    explicit Reader(const std::string &path);

    bool ok() const { return ok_; }
    const Header &header() const { return header_; }
    // Timeline length in slots, from the records themselves (never the
    // header, which can be stale): highest intact slot index.
    int last_slot() const { return last_slot_; }

    struct Record {
        uint32_t slot;
        uint8_t kind;      // RecordKind
        const uint8_t *payload;  // into the Reader's buffer
        size_t len;
    };
    // Next intact record, in file order. False at end (or truncation).
    bool next(Record &out);
    // Peek the next record's slot without consuming (-1 at end).
    int peek_slot() const;

private:
    std::vector<uint8_t> data_;
    Header header_;
    size_t pos_ = 0;
    int last_slot_ = -1;
    bool ok_ = false;
};

class Recorder {
public:
    // path selects the file: current_path() for an offline run,
    // online_path() for an online session (each machine writes its own —
    // see the storage model above).
    // resumed=false: truncates the file and writes a fresh header.
    // resumed=true: the file exists with a matching run_id — records
    // append after the existing ones (the caller verified the match; the
    // first new record should be a keyframe, the resume seam). Offline
    // that is exit→continue; online it is a client whose auto-rejoin
    // rebuilt the game around the same run_id (carried by the snapshots).
    Recorder(uint64_t run_id, uint8_t player_count, bool resumed,
             const std::string &path);

    bool ok() const { return ok_; }
    // Slots are the recorder's own 10 Hz emission count (continued across a
    // resume via last_record_slot), NOT wall or session clock: the caller
    // only emits while the sim runs, so pauses add no slots and the playback
    // timeline (slot * 100 ms) is pure play time.
    int last_slot() const { return last_slot_; }
    // The upcoming slot lands on a keyframe boundary (every 10th slot) —
    // the caller ORs this with its own force conditions (start, resume
    // seam, level rebuild).
    bool keyframe_due() const { return (last_slot_ + 1) % 10 == 0; }

    void record_keyframe(const std::vector<uint8_t> &payload);  // slot = next
    void record_delta(const std::vector<uint8_t> &payload);     // slot = next
    // Events attach to the upcoming slot (they happened after the last
    // emitted record); teed from net_send_event.
    void record_event(uint8_t code, uint32_t arg);
    // Transient weapon visual (REC_EFFECT), same slot rule as events.
    void record_effect(uint8_t subtype, uint8_t player_idx,
                       const std::vector<uint8_t> &body);

    // Hold records again until the next keyframe. The online CLIENT needs
    // this: it records the HOST's keyframes and deltas verbatim, so every
    // delta is encoded against a host keyframe — but on (re)join the
    // bootstrap keyframe went to the LOBBY before the game existed and was
    // never recorded. Substituting a keyframe built from the client's own
    // replica leaves the file's baseline subtly different from the one the
    // following deltas assume, and objects those deltas do not mention keep
    // the error until the next full host keyframe re-seeds it — a
    // correction every second, forever (field: Android client rejoin,
    // 2026-07-27: ~2 corrections/s at ~10 units before the seam, ~20/s at
    // 50-70 units for the whole 100 s after it). Waiting for a REAL host
    // keyframe costs up to a second of records and buys one consistent
    // baseline for the whole file.
    void await_keyframe();

    // Append the in-RAM chunk to current.nrp (checkpoint: level clear,
    // pause, focus loss). No-op when nothing new was recorded.
    void flush();

    // Bank the run's score/generation so the next flush can write them into
    // the header's patchable tail. Without this a run that never reaches
    // finalize (crash, killed tab) keeps the creation values and lists as
    // "SCORE 0 LEVEL 1" however far it actually got.
    void note_progress(uint32_t score, uint32_t generation);

    // Final flush + in-place header patch (score/generation/duration/flags;
    // duration derives from the slot count — pure play time).
    // ended=true (game over): also retires the file — offline rotates
    // current → recent (+best check); online just best-checks in place
    // (online.nrp is the listed slot and never moves).
    // ended=false (abandon to menu): the file stays in place — resumable
    // offline; online it waits for a rejoin-resume or the next session's
    // overwrite.
    // A fresh session that never recorded a DELTA deletes the file instead
    // (zero-tick rule); a resumed session with nothing new leaves the file
    // exactly as it was.
    void finalize(uint32_t score, uint32_t generation, bool cheated,
                  bool ended, uint8_t player_count);

private:
    void note_predawn_drop();  // a record offered before the opening keyframe
    bool patch_header_tail();  // rewrite score/generation/duration; true if it wrote
    // True once the recording has hit its storage cap (web only) — see the
    // definition. Banks what is in RAM the first time it trips.
    bool over_size_cap();
    void append_record(uint32_t slot, uint8_t kind, const uint8_t *data,
                       size_t len);
    bool write_chunk();  // append the RAM chunk (no ok_ gate); true if it wrote
    void retire();       // truly-over run: rotate (offline) / best check (online)

    // Consecutive failed chunk appends before the recording declares
    // itself dead (see write_chunk) instead of growing the RAM chunk
    // for the rest of the run on a disk that never recovers.
    static const int MAX_FAILED_WRITES = 8;

    std::string path_;
    Header header_;
    std::vector<uint8_t> chunk_;  // records since the last flush
    int  failed_writes_ = 0;      // consecutive write_chunk open failures
    int  last_slot_ = -1;
    int  deltas_this_session_ = 0;  // REC_DELTA records this session
    bool resumed_ = false;
    bool ok_ = false;
    // A file must OPEN with a keyframe — playback has no baseline to apply
    // anything against until one arrives, and the reader rejects the whole
    // recording outright ("no leading keyframe"). Events and effects are
    // recorded the moment they happen, but the ONLINE host's opening
    // keyframe rides its 10 Hz send tee and so cannot land for up to
    // 100 ms; anything fired in that window used to be appended first and
    // cost the session its entire recording (field: Android host, online,
    // 2026-07-27 — 317 KB of records, unplayable). Rather than ask two
    // call sites to order themselves correctly, the recorder simply drops
    // records until it has written one. A resumed file already has its
    // leading keyframe, so it starts satisfied.
    bool have_keyframe_ = false;
    int  predawn_drops_ = 0;   // records held out awaiting that keyframe
    // Web only (see record_delta): slot of the last interval flush. IndexedDB
    // commits asynchronously, so a closing tab loses whatever the checkpoint
    // flush had not committed yet — this bounds that to one interval.
    int  last_synced_slot_ = -1;
    // Bytes appended so far. Only used to size the web sync interval: IDBFS
    // re-stores the whole file every sync, so the cost is the file, not the
    // chunk (see record_delta).
    size_t file_bytes_ = 0;
    bool size_capped_ = false;  // cap tripped: recorded, not growing
};

}  // namespace Replay
