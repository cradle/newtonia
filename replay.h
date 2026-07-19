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
// in-RAM chunk; each checkpoint APPENDS the chunk to replays/current.nrp —
// never a whole-file rewrite — and the header's tail fields are patched in
// place only at run end / clean abandon. Three files, all watchable:
//   current.nrp  the active (live or resumable) run
//   recent.nrp   the most recently completed run
//   best.nrp     promoted copy of the highest-scoring non-cheated run
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
    static const uint16_t FORMAT_VERSION = 1;
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
};

// Paths in <pref>/replays/ (created on demand). Empty string on failure.
std::string current_path();
std::string recent_path();
std::string best_path();

uint64_t new_run_id();  // never returns 0

// Reads+validates a header (magic, format version, sane header_size).
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
    // resumed=false: truncates current.nrp and writes a fresh header.
    // resumed=true: current.nrp exists with a matching run_id — records
    // append after the existing ones (the caller verified the match; the
    // first new record should be a keyframe, the resume seam).
    Recorder(uint64_t run_id, uint8_t player_count, bool resumed);

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

    // Append the in-RAM chunk to current.nrp (checkpoint: level clear,
    // pause, focus loss). No-op when nothing new was recorded.
    void flush();

    // Final flush + in-place header patch (score/generation/duration/flags;
    // duration derives from the slot count — pure play time).
    // ended=true (game over): also rotates current → recent (+best check).
    // ended=false (abandon to menu): current stays in place, resumable.
    // A fresh session that never recorded a DELTA deletes the file instead
    // (zero-tick rule); a resumed session with nothing new leaves the file
    // exactly as it was.
    void finalize(uint32_t score, uint32_t generation, bool cheated,
                  bool ended, uint8_t player_count);

private:
    void append_record(uint32_t slot, uint8_t kind, const uint8_t *data,
                       size_t len);
    void write_chunk();  // append the RAM chunk to the file (no ok_ gate)

    std::string path_;
    Header header_;
    std::vector<uint8_t> chunk_;  // records since the last flush
    int  last_slot_ = -1;
    int  deltas_this_session_ = 0;  // REC_DELTA records this session
    bool resumed_ = false;
    bool ok_ = false;
};

}  // namespace Replay
