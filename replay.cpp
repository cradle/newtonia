#include "replay.h"
#include "savegame.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace Replay {

// ── Paths ────────────────────────────────────────────────────────────────────

static std::string dir_path() {
    // Same pref-path org/app as the savegame; replays live in a subdirectory.
    char *dir = SDL_GetPrefPath("cc.gfm", "newtonia");
    if (!dir) return "";
    std::string p = std::string(dir) + "replays";
    SDL_free(dir);
#ifdef _WIN32
    _mkdir(p.c_str());
#else
    mkdir(p.c_str(), 0755);
#endif
    return p + "/";
}

std::string current_path() {
    std::string d = dir_path();
    return d.empty() ? "" : d + "current.nrp";
}
std::string recent_path() {
    std::string d = dir_path();
    return d.empty() ? "" : d + "recent.nrp";
}
std::string best_path() {
    std::string d = dir_path();
    return d.empty() ? "" : d + "best.nrp";
}
std::string online_path() {
    std::string d = dir_path();
    return d.empty() ? "" : d + "online.nrp";
}

// Mirror the savegame's IDBFS behaviour: MEMFS writes only survive a web
// reload once synced to IndexedDB, so every flush/patch/rotation syncs.
static void web_sync() {
#ifdef __EMSCRIPTEN__
    EM_ASM(
        FS.syncfs(false, function(err) {
            if (err) console.error('[newtonia] replay sync failed:', err);
        });
    );
#endif
}

// ── Header pack/unpack ───────────────────────────────────────────────────────
// Fixed 64-byte layout (see replay.h). Packed by hand so the patchable tail
// has a stable byte offset regardless of struct padding.

static void pack_header(uint8_t out[Header::SIZE], const Header &h) {
    memset(out, 0, Header::SIZE);
    uint32_t magic = Header::MAGIC;
    memcpy(out + 0,  &magic, 4);
    memcpy(out + 4,  &h.format_version, 2);
    memcpy(out + 6,  &h.header_size, 2);
    memcpy(out + 8,  h.game_version, Header::GAME_VERSION_LEN);
    memcpy(out + 32, &h.run_id, 8);
    memcpy(out + 40, &h.date, 8);
    // Patchable tail at PATCH_OFFSET (48):
    out[48] = h.flags;
    out[49] = h.player_count;
    memcpy(out + 50, &h.save_version, 2);
    memcpy(out + 52, &h.final_score, 4);
    memcpy(out + 56, &h.generation, 4);
    memcpy(out + 60, &h.duration_ms, 4);
}

static bool unpack_header(const uint8_t in[Header::SIZE], Header &h) {
    uint32_t magic = 0;
    memcpy(&magic, in + 0, 4);
    if (magic != Header::MAGIC) return false;
    memcpy(&h.format_version, in + 4, 2);
    memcpy(&h.header_size, in + 6, 2);
    if (h.format_version < 1 || h.format_version > Header::FORMAT_VERSION)
        return false;
    if (h.header_size < Header::SIZE || h.header_size > 4096) return false;
    memcpy(h.game_version, in + 8, Header::GAME_VERSION_LEN);
    h.game_version[Header::GAME_VERSION_LEN - 1] = '\0';
    memcpy(&h.run_id, in + 32, 8);
    memcpy(&h.date, in + 40, 8);
    h.flags = in[48];
    h.player_count = in[49];
    memcpy(&h.save_version, in + 50, 2);
    memcpy(&h.final_score, in + 52, 4);
    memcpy(&h.generation, in + 56, 4);
    memcpy(&h.duration_ms, in + 60, 4);
    return true;
}

bool read_header(const std::string &path, Header &h) {
    if (path.empty()) return false;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    uint8_t buf[Header::SIZE];
    bool ok = fread(buf, 1, Header::SIZE, fp) == Header::SIZE;
    fclose(fp);
    return ok && unpack_header(buf, h);
}

// ── Record scan ──────────────────────────────────────────────────────────────

// Sanity bound for a single record: a keyframe at a late generation is tens
// of KB; anything past this is corruption, stop scanning.
static const uint32_t MAX_RECORD_BYTES = 8u * 1024u * 1024u;

// Walk the record framing. stop_at_delta: return as soon as a DELTA is
// seen (existence check). Otherwise walks every intact record and reports
// the highest slot. A truncated final record (crash artifact) simply ends
// the walk — exactly how a reader treats it.
static void scan_records(const std::string &path, bool stop_at_delta,
                         bool *has_delta, int *last_slot) {
    if (has_delta) *has_delta = false;
    if (last_slot) *last_slot = -1;
    Header h;
    if (!read_header(path, h)) return;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    if (fseek(fp, (long)h.header_size, SEEK_SET) == 0) {
        for (;;) {
            long pos = ftell(fp);
            uint32_t slot = 0, len = 0;
            uint8_t kind = 0;
            if (fread(&slot, 4, 1, fp) != 1) break;
            if (fread(&kind, 1, 1, fp) != 1) break;
            if (fread(&len, 4, 1, fp) != 1) break;
            if (len > MAX_RECORD_BYTES) break;
            // Only count a record whose payload is fully present — a
            // truncated final record (crash artifact) ends the walk.
            if (pos + 9 + (long)len > fsize) break;
            if (fseek(fp, (long)len, SEEK_CUR) != 0) break;
            if (last_slot && (int)slot > *last_slot) *last_slot = (int)slot;
            if (kind == REC_DELTA && has_delta) {
                *has_delta = true;
                if (stop_at_delta) break;
            }
        }
    }
    fclose(fp);
}

bool has_delta_record(const std::string &path) {
    bool found = false;
    scan_records(path, true, &found, NULL);
    return found;
}

int last_record_slot(const std::string &path) {
    int last = -1;
    scan_records(path, false, NULL, &last);
    return last;
}

// ── Reader ───────────────────────────────────────────────────────────────────

Reader::Reader(const std::string &path) {
    if (path.empty()) return;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize < (long)Header::SIZE || fsize > 512L * 1024L * 1024L) {
        fclose(fp);
        return;
    }
    data_.resize((size_t)fsize);
    bool read_ok = fread(&data_[0], 1, data_.size(), fp) == data_.size();
    fclose(fp);
    if (!read_ok || !unpack_header(&data_[0], header_)) {
        data_.clear();
        return;
    }
    pos_ = header_.header_size;
    // Timeline length from the records, not the (possibly stale) header.
    last_slot_ = last_record_slot(path);
    ok_ = true;
}

// True while an intact record starts at pos_ (frame + full payload).
static bool record_at(const std::vector<uint8_t> &data, size_t pos,
                      uint32_t *slot, uint8_t *kind, uint32_t *len) {
    if (pos + 9 > data.size()) return false;
    memcpy(slot, &data[pos], 4);
    *kind = data[pos + 4];
    memcpy(len, &data[pos + 5], 4);
    if (*len > MAX_RECORD_BYTES) return false;
    if (pos + 9 + *len > data.size()) return false;
    return true;
}

bool Reader::next(Record &out) {
    uint32_t slot = 0, len = 0;
    uint8_t kind = 0;
    if (!ok_ || !record_at(data_, pos_, &slot, &kind, &len)) return false;
    out.slot = slot;
    out.kind = kind;
    out.payload = len ? &data_[pos_ + 9] : NULL;
    out.len = len;
    pos_ += 9 + len;
    return true;
}

int Reader::peek_slot() const {
    uint32_t slot = 0, len = 0;
    uint8_t kind = 0;
    if (!ok_ || !record_at(data_, pos_, &slot, &kind, &len)) return -1;
    return (int)slot;
}

// ── Run ids ──────────────────────────────────────────────────────────────────

uint64_t new_run_id() {
    // Not security-sensitive — just needs to make save↔replay collisions
    // implausible. random_device seeded and mixed with wall/boot time in
    // case a platform's random_device is weak.
    std::random_device rd;
    uint64_t id = ((uint64_t)rd() << 32) ^ (uint64_t)rd();
    id ^= (uint64_t)time(NULL) << 20;
    id ^= (uint64_t)SDL_GetTicks();
    if (id == 0) id = 1;
    return id;
}

// ── Rotation ─────────────────────────────────────────────────────────────────

static bool copy_file(const std::string &from, const std::string &to) {
    FILE *src = fopen(from.c_str(), "rb");
    if (!src) return false;
    FILE *dst = fopen(to.c_str(), "wb");
    if (!dst) { fclose(src); return false; }
    uint8_t buf[64 * 1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        if (fwrite(buf, 1, n, dst) != n) { ok = false; break; }
    ok = ok && !ferror(src);
    fclose(src);
    if (fclose(dst) != 0) ok = false;
    return ok;
}

// Shared rotation body: FROM (current.nrp or online.nrp) becomes recent,
// after the zero-tick junk check and the best promotion. `what` labels the
// log line only.
static void rotate_to_recent(const std::string &from, const char *what) {
    if (from.empty()) return;
    Header h;
    if (!read_header(from, h)) {
        // Unreadable junk (or nothing there) — just make sure it's gone.
        std::remove(from.c_str());
        return;
    }
    if (!has_delta_record(from)) {
        // Zero-tick rule: a run with no sim records never becomes recent.
        std::remove(from.c_str());
        web_sync();
        return;
    }
    // Best check — only where the header is known accurate (FLAG_CLEAN: the
    // tail was patched at a clean stop). A crashed run's stale header gets
    // no check (REPLAY.md accepted limitation): it still lands in recent,
    // it just can't become best.
    if ((h.flags & FLAG_CLEAN) && !(h.flags & FLAG_CHEATED)) {
        Header hb;
        bool have_best = read_header(best_path(), hb);
        if (!have_best || h.final_score > hb.final_score)
            copy_file(from, best_path());
    }
    std::remove(recent_path().c_str());
    if (std::rename(from.c_str(), recent_path().c_str()) != 0) {
        // Cross-volume or locked-file fallback: copy then delete.
        if (copy_file(from, recent_path())) std::remove(from.c_str());
    }
    web_sync();
    SDL_Log("replay: rotated %s -> recent (score=%u gen=%u%s%s)", what,
            h.final_score, h.generation,
            (h.flags & FLAG_CLEAN) ? "" : " stale-header",
            (h.flags & FLAG_CHEATED) ? " cheated" : "");
}

void rotate_current_to_recent() { rotate_to_recent(current_path(), "current"); }

static bool file_exists(const std::string &path) {
    if (path.empty()) return false;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}

void on_new_game() {
    if (file_exists(current_path())) rotate_current_to_recent();
}

void rotate_online_to_recent() {
    if (file_exists(online_path())) rotate_to_recent(online_path(), "online");
}

// ── Recorder ─────────────────────────────────────────────────────────────────

Recorder::Recorder(uint64_t run_id, uint8_t player_count, bool resumed,
                   const std::string &path)
    : resumed_(resumed) {
    path_ = path;
    if (path_.empty()) return;

    if (resumed) {
        // Caller verified the run_id matches; keep the existing header (its
        // creation fields are immutable) and append after the existing
        // records, continuing their slot numbering so the timeline stays
        // continuous. The first new record must be a keyframe (resume
        // seam) — the caller's force-keyframe state guarantees that.
        ok_ = read_header(path_, header_) && header_.run_id == run_id;
        if (!ok_) return;
        last_slot_ = last_record_slot(path_);
        SDL_Log("replay: resuming recording (run_id=%llx from slot %d)",
                (unsigned long long)run_id, last_slot_);
        return;
    }

    header_ = Header();
    strncpy(header_.game_version, NEWTONIA_VERSION_STRING,
            Header::GAME_VERSION_LEN - 1);
    header_.run_id = run_id;
    header_.date = (uint64_t)time(NULL);
    header_.player_count = player_count;
    // The record payloads serialize with this build's savegame format;
    // playback needs the number to parse them across future bumps.
    header_.save_version = Save::GameState::VERSION;

    FILE *fp = fopen(path_.c_str(), "wb");
    if (!fp) return;
    uint8_t buf[Header::SIZE];
    pack_header(buf, header_);
    ok_ = fwrite(buf, 1, Header::SIZE, fp) == Header::SIZE;
    fclose(fp);
    if (ok_)
        SDL_Log("replay: recording started (run_id=%llx)",
                (unsigned long long)run_id);
}

void Recorder::append_record(uint32_t slot, uint8_t kind, const uint8_t *data,
                             size_t len) {
    if (!ok_) return;
    uint32_t l = (uint32_t)len;
    const uint8_t *sp = (const uint8_t *)&slot;
    const uint8_t *lp = (const uint8_t *)&l;
    chunk_.insert(chunk_.end(), sp, sp + 4);
    chunk_.push_back(kind);
    chunk_.insert(chunk_.end(), lp, lp + 4);
    if (len) chunk_.insert(chunk_.end(), data, data + len);
}

void Recorder::record_keyframe(const std::vector<uint8_t> &payload) {
    last_slot_++;
    append_record((uint32_t)last_slot_, REC_KEYFRAME,
                  payload.empty() ? NULL : &payload[0], payload.size());
}

void Recorder::record_delta(const std::vector<uint8_t> &payload) {
    last_slot_++;
    append_record((uint32_t)last_slot_, REC_DELTA,
                  payload.empty() ? NULL : &payload[0], payload.size());
    deltas_this_session_++;
}

void Recorder::record_event(uint8_t code, uint32_t arg) {
    uint8_t payload[5];
    payload[0] = code;
    memcpy(payload + 1, &arg, 4);
    append_record((uint32_t)(last_slot_ + 1), REC_EVENTS, payload,
                  sizeof(payload));
}

void Recorder::record_effect(uint8_t subtype, uint8_t player_idx,
                             const std::vector<uint8_t> &body) {
    std::vector<uint8_t> payload;
    payload.reserve(2 + body.size());
    payload.push_back(subtype);
    payload.push_back(player_idx);
    payload.insert(payload.end(), body.begin(), body.end());
    append_record((uint32_t)(last_slot_ + 1), REC_EFFECT,
                  payload.empty() ? NULL : &payload[0], payload.size());
}

const char *Recorder::rotate_label() const {
    return path_ == online_path() ? "online" : "current";
}

void Recorder::write_chunk() {
    if (chunk_.empty()) return;
    FILE *fp = fopen(path_.c_str(), "ab");
    if (!fp) return;
    // A short write leaves a truncated final record; the reader detects and
    // drops it (self-delimiting framing), so no cleanup is attempted here.
    fwrite(&chunk_[0], 1, chunk_.size(), fp);
    fclose(fp);
    chunk_.clear();
    web_sync();
}

void Recorder::flush() {
    if (!ok_) return;
    write_chunk();
}

void Recorder::finalize(uint32_t score, uint32_t generation, bool cheated,
                        bool ended, uint8_t player_count) {
    if (!ok_) return;
    ok_ = false;  // one-shot; further record/flush calls become no-ops

    // Zero-tick rule: a fresh session that never advanced the sim leaves no
    // replay (delete the header-only/keyframe-only file); a resumed session
    // with nothing new leaves the existing file exactly as it was.
    if (deltas_this_session_ == 0) {
        chunk_.clear();
        if (!resumed_) {
            std::remove(path_.c_str());
            web_sync();
        }
        // ended with a resumed file: the run is still over — rotate what the
        // earlier sessions recorded.
        if (resumed_ && ended) rotate_to_recent(path_, rotate_label());
        return;
    }

    write_chunk();

    header_.flags = (uint8_t)((cheated ? FLAG_CHEATED : 0) | FLAG_CLEAN |
                              (ended ? FLAG_ENDED : 0));
    if (player_count > header_.player_count) header_.player_count = player_count;
    header_.final_score = score;
    header_.generation = generation;
    // Timeline length = slot count * the 10 Hz cadence: pure play time
    // (pauses emitted no slots), continuous across resumes.
    header_.duration_ms = (uint32_t)((last_slot_ + 1) * 100);

    FILE *fp = fopen(path_.c_str(), "r+b");
    if (fp) {
        uint8_t buf[Header::SIZE];
        pack_header(buf, header_);
        if (fseek(fp, (long)Header::PATCH_OFFSET, SEEK_SET) == 0)
            fwrite(buf + Header::PATCH_OFFSET, 1,
                   Header::SIZE - Header::PATCH_OFFSET, fp);
        fclose(fp);
        web_sync();
    }
    SDL_Log("replay: %s (score=%u gen=%u dur=%ums%s)",
            ended ? "run ended" : "abandoned (resumable)", score, generation,
            header_.duration_ms, cheated ? " cheated" : "");

    if (ended) rotate_to_recent(path_, rotate_label());
}

}  // namespace Replay
