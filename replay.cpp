#include "web_fs.h"
#include "replay.h"
#include "savegame.h"
#include "preferences.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
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
std::string best_coop_path() {
    std::string d = dir_path();
    return d.empty() ? "" : d + "best_coop.nrp";
}
std::string online_path() {
    std::string d = dir_path();
    return d.empty() ? "" : d + "online.nrp";
}
std::string download_path() {
    std::string d = dir_path();
    return d.empty() ? "" : d + "download.nrp";
}

std::string game_version_string() {
    // The same silent 23-char truncation the header stamp applies
    // (Recorder ctor) — the leaderboard's season queries must produce the
    // byte-identical string a recorded header carries.
    char buf[Header::GAME_VERSION_LEN];
    strncpy(buf, NEWTONIA_VERSION_STRING, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

// MEMFS writes only survive a web reload once synced to IndexedDB, so every
// flush/patch/rotation syncs. Goes through the shared coalescer: a rotation
// renames and deletes files while the flush that preceded it may still be
// mid-sync, which used to fail that sync outright (web_fs.h).
static void web_sync() { web_fs_sync("replay"); }

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

static HeaderStatus unpack_header(const uint8_t in[Header::SIZE], Header &h) {
    uint32_t magic = 0;
    memcpy(&magic, in + 0, 4);
    if (magic != Header::MAGIC) return HEADER_DAMAGED;
    memcpy(&h.format_version, in + 4, 2);
    memcpy(&h.header_size, in + 6, 2);
    // Version 0 is not a format anyone ever wrote — that is damage, not age.
    if (h.format_version == 0) return HEADER_DAMAGED;
    if (h.format_version < Header::MIN_FORMAT_VERSION) return HEADER_TOO_OLD;
    if (h.format_version > Header::FORMAT_VERSION) return HEADER_TOO_NEW;
    if (h.header_size < Header::SIZE || h.header_size > 4096)
        return HEADER_DAMAGED;
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
    return HEADER_OK;
}

HeaderStatus read_header_status(const std::string &path, Header &h) {
    if (path.empty()) return HEADER_DAMAGED;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return HEADER_DAMAGED;
    uint8_t buf[Header::SIZE];
    bool full = fread(buf, 1, Header::SIZE, fp) == Header::SIZE;
    fclose(fp);
    // Short of a whole header there is nothing to version-check.
    if (!full) return HEADER_DAMAGED;
    return unpack_header(buf, h);
}

bool read_header(const std::string &path, Header &h) {
    return read_header_status(path, h) == HEADER_OK;
}

// ── Record scan ──────────────────────────────────────────────────────────────

// Sanity bound for a single record: a keyframe at a late generation is tens
// of KB; anything past this is corruption, stop scanning.
static const uint32_t MAX_RECORD_BYTES = 8u * 1024u * 1024u;

// Sanity bound for a slot index, for the same reason and enforced in the same
// places. Slots are a 10 Hz count, so this is 23 days of continuous play —
// but it is chosen so that `slot * 100` (the ms timeline every consumer
// computes, e.g. GLGame::replay_clock_ms_ and the HUD's total) stays inside a
// signed 32-bit int. A file-controlled u32 multiplied by 100 is otherwise
// signed overflow before anyone gets to range-check the result.
static const uint32_t MAX_RECORD_SLOT = 20u * 1000u * 1000u;

// Walk the record framing. stop_at_delta: return as soon as a DELTA is
// seen (existence check). Otherwise walks every intact record and reports
// the highest slot. A truncated final record (crash artifact) simply ends
// the walk — exactly how a reader treats it.
// intact_end (walk mode only) receives the file offset just past the last
// INTACT record, i.e. where a reader stops: appending anywhere else puts
// records behind a truncated one, where nothing can ever read them.
static void scan_records(const std::string &path, bool stop_at_delta,
                         bool *has_delta, int *last_slot,
                         int64_t *intact_end = NULL) {
    if (has_delta) *has_delta = false;
    if (last_slot) *last_slot = -1;
    if (intact_end) *intact_end = -1;
    Header h;
    if (!read_header(path, h)) return;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    // Offsets as 64-bit: `long` is 32 bits on wasm32 and Windows alike, and
    // this walk (unlike Reader) has no file-size cap in front of it.
    int64_t fsize = (int64_t)ftell(fp);
    if (fseek(fp, (long)h.header_size, SEEK_SET) == 0) {
        if (intact_end) *intact_end = (int64_t)h.header_size;
        for (;;) {
            int64_t pos = (int64_t)ftell(fp);
            uint32_t slot = 0, len = 0;
            uint8_t kind = 0;
            if (fread(&slot, 4, 1, fp) != 1) break;
            if (fread(&kind, 1, 1, fp) != 1) break;
            if (fread(&len, 4, 1, fp) != 1) break;
            if (len > MAX_RECORD_BYTES || slot > MAX_RECORD_SLOT) break;
            // Only count a record whose payload is fully present — a
            // truncated final record (crash artifact) ends the walk.
            if (pos + 9 + (int64_t)len > fsize) break;
            if (fseek(fp, (long)len, SEEK_CUR) != 0) break;
            if (intact_end) *intact_end = pos + 9 + (int64_t)len;
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

// Cut a file back to `size` bytes. Only ever used to remove a truncated
// final record before appending after it (see Recorder's resumed branch and
// write_chunk): records written behind one are unreachable — the reader, and
// every scan here, stops at the break — so a resumed session would have
// banked its whole run into a part of the file nothing can read.
static bool truncate_file(const std::string &path, int64_t size) {
    if (path.empty() || size < 0) return false;
#ifdef _WIN32
    FILE *fp = fopen(path.c_str(), "r+b");
    if (!fp) return false;
    bool ok = _chsize_s(_fileno(fp), (__int64)size) == 0;
    fclose(fp);
    return ok;
#else
    return truncate(path.c_str(), (off_t)size) == 0;
#endif
}

// File size in bytes (-1 when unreadable).
static int64_t file_size_of(const std::string &path) {
    if (path.empty()) return -1;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    int64_t n = (int64_t)ftell(fp);
    fclose(fp);
    return n;
}

// ── Reader ───────────────────────────────────────────────────────────────────

// True while an intact record starts at pos (frame + full payload).
static bool record_at(const std::vector<uint8_t> &data, size_t pos,
                      uint32_t *slot, uint8_t *kind, uint32_t *len);

Reader::Reader(const std::string &path) {
    if (path.empty()) return;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // The whole file is buffered before anything is validated, so the cap is
    // what stands between a hostile/corrupt length and an allocation the
    // platform cannot serve. On web that must be a DECLINE, not an OOM abort:
    // the recorder caps its own files at 32 MB there (over_size_cap) and the
    // heap is a fraction of a native one, so anything much past the cap is
    // not a file this build wrote.
#ifdef __EMSCRIPTEN__
    const long MAX_FILE_BYTES = 48L * 1024L * 1024L;
#else
    const long MAX_FILE_BYTES = 512L * 1024L * 1024L;
#endif
    if (fsize < (long)Header::SIZE || fsize > MAX_FILE_BYTES) {
        fclose(fp);
        return;
    }
    data_.resize((size_t)fsize);
    bool read_ok = fread(&data_[0], 1, data_.size(), fp) == data_.size();
    fclose(fp);
    // Compare against HEADER_OK explicitly: unpack_header returns a status,
    // not a bool, and HEADER_OK is 0 — so a bare `!unpack_header(...)` reads
    // as "failed" while meaning "succeeded".
    if (!read_ok || unpack_header(&data_[0], header_) != HEADER_OK) {
        data_.clear();
        return;
    }
    pos_ = header_.header_size;
    // Timeline length from the records, not the (possibly stale) header —
    // walked over the buffer we just read, NOT via last_record_slot(path),
    // which reopens the file and reads every byte a second time. On a
    // 20 MB replay that was 40 MB of I/O and two full passes to open a
    // playback.
    last_slot_ = -1;
    for (size_t p = pos_;;) {
        uint32_t slot = 0, len = 0;
        uint8_t kind = 0;
        if (!record_at(data_, p, &slot, &kind, &len)) break;
        if ((int)slot > last_slot_) last_slot_ = (int)slot;
        p += 9 + len;
    }
    ok_ = true;
}

static bool record_at(const std::vector<uint8_t> &data, size_t pos,
                      uint32_t *slot, uint8_t *kind, uint32_t *len) {
    if (pos + 9 > data.size()) return false;
    memcpy(slot, &data[pos], 4);
    *kind = data[pos + 4];
    memcpy(len, &data[pos + 5], 4);
    if (*len > MAX_RECORD_BYTES || *slot > MAX_RECORD_SLOT) return false;
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

// Call only AFTER load_preferences(): minting saves, so an early call would
// write a defaults-only g_prefs over the player's INI. The one production
// caller is GLGame::replay_start, long past startup.
uint64_t run_id_salt() {
    if (g_prefs.net_run_id_salt == 0) {
        // Same mix as new_run_id: random_device, plus wall/boot time in case
        // a platform's random_device is weak.
        std::random_device rd;
        uint64_t s = ((uint64_t)rd() << 32) ^ (uint64_t)rd();
        s ^= (uint64_t)time(NULL) << 20;
        s ^= (uint64_t)SDL_GetTicks();
        // Never 0 — that would make the client record under the HOST's id and
        // collide with its row.
        if (s == 0) s = 0x9e3779b97f4a7c15ULL;
        g_prefs.net_run_id_salt = s;
        save_preferences();
        SDL_Log("replay: minted this install's run-id salt");
    }
    return g_prefs.net_run_id_salt;
}

// ── Rotation ─────────────────────────────────────────────────────────────────

static bool copy_file(const std::string &from, const std::string &to) {
    FILE *src = fopen(from.c_str(), "rb");
    if (!src) return false;
    FILE *dst = fopen(to.c_str(), "wb");
    if (!dst) { fclose(src); return false; }
    // HEAP, not stack. This was `uint8_t buf[64 * 1024]` — and emscripten's
    // default stack is 64 KB exactly, so the buffer WAS the whole stack and
    // every call blew it: "Aborted(stack overflow ... stack limits
    // [0x00065da0 - 0x00075da0])", a 0x10000 span. Native platforms have
    // megabytes of stack and never noticed.
    //
    // It surfaced the day recording became the default, because the only
    // caller that runs routinely is maybe_promote_best — and promotion skips
    // cheat-flagged runs, so it takes an ordinary clean run to reach. Field
    // reports on the itch build showed it as "index out of bounds" and
    // "Aborted()" in the main loop, both immediately after "promoted best".
    std::vector<uint8_t> buf(64 * 1024);
    size_t n;
    bool ok = true;
    while ((n = fread(&buf[0], 1, buf.size(), src)) > 0)
        if (fwrite(&buf[0], 1, n, dst) != n) { ok = false; break; }
    ok = ok && !ferror(src);
    fclose(src);
    if (fclose(dst) != 0) ok = false;
    return ok;
}

// Best promotion — only where the header is known accurate (FLAG_CLEAN:
// the tail was patched at a clean stop). A crashed run's stale header gets
// no check (REPLAY.md accepted limitation): the run stays watchable in its
// slot, it just can't become best.
// See take_best_promoted() in replay.h — one-shot path for the
// leaderboard's game-over prompt.
static std::string g_best_promoted_path;

std::string take_best_promoted() {
    std::string v = g_best_promoted_path;
    g_best_promoted_path.clear();
    return v;
}

// One-time split migration: a best.nrp written before the co-op slot
// existed may BE a co-op run — move it to its own slot so it competes on
// the right board and the solo slot reopens for solo runs. Runs lazily
// ahead of every promotion check and every best_path_for resolution (the
// menu's reads all route through that); idempotent (post-split the solo
// slot's header can only be solo).
static void ensure_best_split() {
    static bool checked = false;
    if (checked) return;
    checked = true;
    Header h;
    if (!read_header(best_path(), h) || h.player_count < 2) return;
    FILE *fp = fopen(best_coop_path().c_str(), "rb");
    if (fp) { fclose(fp); return; }  // both slots live — nothing to move
    if (std::rename(best_path().c_str(), best_coop_path().c_str()) != 0) {
        if (copy_file(best_path(), best_coop_path()))
            std::remove(best_path().c_str());
    }
    web_sync();
    SDL_Log("replay: moved co-op best into its own slot (score=%u)",
            h.final_score);
}

// Best is PER-BOARD (LEADERBOARD.md): the worker keeps solo and co-op
// tables apart, so the local best must too — with one shared slot a solo
// high score silently shadowed every later co-op run (the co-op score
// never beat it, so the run never promoted and the CO-OP board never got
// its upload prompt; field, 2026-08-01). player_count picks the slot.
std::string best_path_for(uint8_t player_count) {
    ensure_best_split();
    return player_count >= 2 ? best_coop_path() : best_path();
}

static void maybe_promote_best(const std::string &from, const Header &h) {
    if (!(h.flags & FLAG_CLEAN) || (h.flags & FLAG_CHEATED)) return;
    ensure_best_split();
    std::string slot = best_path_for(h.player_count);
    Header hb;
    bool have_best = read_header(slot, hb);
    // A scoreless run never promotes AT ALL — the worker refuses score-0
    // submissions, so promoting one only arms a doomed upload prompt.
    // This used to guard only the season-change arm; the !have_best arm
    // let a clean 0-score run claim an EMPTY slot, which the per-player
    // co-op claims made routine (an idle online partner finalizes a
    // clean 0-score recording, and everyone's best_coop.nrp starts
    // empty): the next game over then prompted for an upload that could
    // only fail (review, 2026-08-02).
    if (h.final_score == 0) return;
    // Best is SEASON-scoped (LEADERBOARD.md): a clean run whose season
    // differs from the stored best's promotes regardless of score — a
    // fresh season starts from a clean slate, and gating on the old
    // season's high score would keep any lower (but board-qualifying)
    // run from ever prompting for upload. best.nrp is therefore "your
    // best run of the season you last played"; the old season's uploaded
    // replay lives on on its board.
    bool season_changed = have_best &&
        memcmp(h.game_version, hb.game_version,
               Header::GAME_VERSION_LEN) != 0;
    if (!have_best || season_changed || h.final_score > hb.final_score) {
        if (copy_file(from, slot)) g_best_promoted_path = slot;
        // Sync the promotion itself: the online retirement path
        // (best_check_online) has no later sync of its own, so without
        // this a new best set in the session's last moments never left
        // MEMFS — closing the tab on the GAME OVER card lost it. The
        // offline path syncs again in rotate_to_recent; web_fs coalesces
        // the pair into one store.
        web_sync();
        SDL_Log("replay: promoted best (%s score=%u gen=%u%s)",
                h.player_count >= 2 ? "co-op" : "solo", h.final_score,
                h.generation, season_changed ? " new season" : "");
    }
}

// current.nrp becomes recent, after the zero-tick junk check and the best
// promotion.
static void rotate_to_recent(const std::string &from) {
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
    maybe_promote_best(from, h);
    // Never destroy the old recent until its replacement is in place. The
    // remove used to come FIRST, to clear the way for the rename — so a
    // rename that failed AND a copy that failed left the player with
    // neither run: the previous recent deleted and the new one still
    // sitting in current. rename() overwrites an existing destination on
    // POSIX and Windows alike, and the copy fallback opens the destination
    // with "wb", so neither needs the clearing.
    if (std::rename(from.c_str(), recent_path().c_str()) != 0) {
        // Cross-volume or locked-file fallback: copy then delete.
        if (copy_file(from, recent_path())) std::remove(from.c_str());
    }
    web_sync();
    SDL_Log("replay: rotated current -> recent (score=%u gen=%u%s%s)",
            h.final_score, h.generation,
            (h.flags & FLAG_CLEAN) ? "" : " stale-header",
            (h.flags & FLAG_CHEATED) ? " cheated" : "");
}

void rotate_current_to_recent() { rotate_to_recent(current_path()); }

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

void best_check_online() {
    Header h;
    if (!read_header(online_path(), h)) return;
    if (!has_delta_record(online_path())) return;
    maybe_promote_best(online_path(), h);
}

// ── Recorder ─────────────────────────────────────────────────────────────────

Recorder::Recorder(uint64_t run_id, uint8_t player_count, bool resumed,
                   const std::string &path)
    : resumed_(resumed), have_keyframe_(resumed) {
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
        int64_t intact_end = -1;
        scan_records(path_, false, NULL, &last_slot_, &intact_end);
        // A resumed file can legitimately hold ZERO records: the header is
        // written at construction but the chunk is RAM-only until a flush,
        // so die-once → killed process → CONTINUE resumes a header-only
        // file. Starting that "keyframe-satisfied" let an effect recorded
        // in the first 100 ms land AHEAD of the seam keyframe, and playback
        // then rejected the entire session ("no leading keyframe"). With no
        // records banked, this is a fresh file in all but name — gate like
        // one.
        if (last_slot_ < 0) have_keyframe_ = false;
        if (intact_end < (int64_t)header_.header_size)
            intact_end = (int64_t)header_.header_size;
        // The leftover may end in a TRUNCATED record — the crash artifact
        // the reader is documented to tolerate. Tolerating it on READ is not
        // enough here: "ab" appends after the break, where the reader never
        // arrives, so this whole session would have recorded into a void
        // while patching an honest-looking header in front of it. Cut the
        // stub off first; it was never readable anyway.
        int64_t fsize = file_size_of(path_);
        if (fsize > intact_end) {
            SDL_Log("replay: trimming %d unreadable byte(s) from the previous "
                    "session's truncated tail before appending",
                    (int)(fsize - intact_end));
            if (!truncate_file(path_, intact_end)) {
                // Appending anyway would silently lose the whole session.
                SDL_Log("replay: cannot trim %s - not resuming", path_.c_str());
                ok_ = false;
                return;
            }
            web_sync();
        }
        // Records already in the file, so the web sync interval scales with
        // the WHOLE file (its sync cost is the file, not the chunk) and the
        // size cap counts what is already on disk. Starting from zero here
        // put a resumed web recording back on a 5 s sync of a multi-MB file
        // — the frozen-tab symptom the scaling exists to prevent — and let
        // a resumed run grow past the cap that protects the player's save.
        file_bytes_ = (size_t)(intact_end - (int64_t)header_.header_size);
        SDL_Log("replay: resuming recording (run_id=%llx from slot %d, "
                "%u KB on disk)",
                (unsigned long long)run_id, last_slot_,
                (unsigned)(file_bytes_ / 1024));
        return;
    }

    header_ = Header();
    strncpy(header_.game_version, NEWTONIA_VERSION_STRING,
            Header::GAME_VERSION_LEN - 1);
    // The field is write-once and the season key R4 buckets on, so a build
    // whose version string doesn't fit says so rather than truncating
    // silently. The build systems keep `git describe` short enough
    // (--abbrev=7 --dirty=+); a hand-passed NEWTONIA_VERSION can still
    // overflow, and this is the only place that would ever notice.
    if (strlen(NEWTONIA_VERSION_STRING) > Header::GAME_VERSION_LEN - 1) {
        SDL_Log("replay: WARNING version stamp truncated to \"%s\" (%u chars, "
                "max %u)", header_.game_version,
                (unsigned)strlen(NEWTONIA_VERSION_STRING),
                (unsigned)(Header::GAME_VERSION_LEN - 1));
    }
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
    // The reader refuses any record longer than MAX_RECORD_BYTES and stops
    // the walk there, so writing one would silently truncate the recording
    // from this point on — a file that looks like an ordinary crash
    // artifact while the run continued for another ten minutes. Nothing
    // approaches 8 MB today (a generation-48 keyframe is tens of KB), which
    // is exactly why the writer must say so if it ever does.
    if (len > MAX_RECORD_BYTES) {
        SDL_Log("replay: recording stopped (record of %u bytes exceeds the "
                "%u-byte limit the reader accepts)",
                (unsigned)len, (unsigned)MAX_RECORD_BYTES);
        chunk_.clear();
        ok_ = false;
        return;
    }
    // Same rule for the slot index (23 days of play): past it the reader
    // stops the walk, so the recording would silently end here while
    // appearing to continue.
    if (slot > MAX_RECORD_SLOT) {
        SDL_Log("replay: recording stopped (slot %u past the %u the reader "
                "accepts)", (unsigned)slot, (unsigned)MAX_RECORD_SLOT);
        chunk_.clear();
        ok_ = false;
        return;
    }
    uint32_t l = (uint32_t)len;
    const uint8_t *sp = (const uint8_t *)&slot;
    const uint8_t *lp = (const uint8_t *)&l;
    chunk_.insert(chunk_.end(), sp, sp + 4);
    chunk_.push_back(kind);
    chunk_.insert(chunk_.end(), lp, lp + 4);
    if (len) chunk_.insert(chunk_.end(), data, data + len);
}

// Records offered before the opening keyframe are dropped (see
// have_keyframe_). Normal in the online host's first 100 ms, so this stays
// quiet after the first one — but it must not be SILENT: if the keyframe
// never arrives, every record is dropped and the session ends with an
// empty recording, and this line is the only thing that would say why.
void Recorder::note_predawn_drop() {
    if (predawn_drops_++ == 0)
        SDL_Log("replay: holding records until the opening keyframe");
}

void Recorder::record_keyframe(const std::vector<uint8_t> &payload) {
    if (have_keyframe_ && over_size_cap()) return;  // never drop the FIRST one
    if (!have_keyframe_ && predawn_drops_ > 0) {
        SDL_Log("replay: opening keyframe written (%d record(s) held out)",
                predawn_drops_);
        // Per-seam, not per-session: await_keyframe can re-open the window
        // on a client rejoin, and a cumulative total there reported every
        // record the session had EVER held rather than the ones this seam
        // did (5 where 2 were held, in the very case the line exists to
        // explain).
        predawn_drops_ = 0;
    }
    have_keyframe_ = true;  // the file now has its baseline
    last_slot_++;
    append_record((uint32_t)last_slot_, REC_KEYFRAME,
                  payload.empty() ? NULL : &payload[0], payload.size());
}

void Recorder::record_delta(const std::vector<uint8_t> &payload) {
    if (!have_keyframe_) return note_predawn_drop();
    if (over_size_cap()) return;
    last_slot_++;
    append_record((uint32_t)last_slot_, REC_DELTA,
                  payload.empty() ? NULL : &payload[0], payload.size());
    deltas_this_session_++;

#ifdef __EMSCRIPTEN__
    // Web only: flush on a slot interval as well as at the checkpoints.
    //
    // Every other platform gets its last flush in before the process dies —
    // Android in onPause, Xbox before the suspend ack, desktop on focus
    // loss — because the write is synchronous. The web's is not: a flush
    // writes MEMFS and schedules FS.syncfs, and IndexedDB commits on a
    // later turn of the event loop. A closing tab does not survive to see
    // it. Measured in-container (headless Chromium, 2026-07-29): the
    // pagehide hook firing and the tab closing immediately persisted 26
    // records, while the identical run given 2.5 s before the close
    // persisted 171 — the flush is right, the commit just never lands.
    //
    // So bound the loss instead of chasing the close: flush on a slot
    // interval (10 Hz emission, so 50 slots = 5 s of play) as well as at the
    // checkpoints. The lifecycle hooks stay for the case the browser DOES
    // grant time (tab hidden, then closed later).
    //
    // The interval GROWS with the recording, because IDBFS has no partial
    // write: every sync re-stores the WHOLE file. Measured in Chromium
    // (2026-07-29): 0.5 MB syncs in ~5 ms, 2 MB in 77, 8 MB in 203, 20 MB in
    // 560 — all on the main thread. A fixed 5 s interval therefore costs
    // more and more as the run goes on, and since each in-flight sync also
    // holds its own copy of the data, a long Safari session ended up with
    // emscripten's own "3 FS.syncfs operations in flight at once" warning,
    // a frozen tab and eventually a crash (field, 2026-07-29). Scaling the
    // interval with the file keeps the IDB write rate roughly flat at about
    // 100 KB/s: 5 s under half a MB, 25 s at 2 MB, ~85 s at 8 MB. The loss
    // window grows with it, which is the honest trade — the alternative is
    // a tab that stops responding, and every checkpoint still flushes.
    // web_fs.h's coalescer is the other half: never two syncs at once.
    int interval = 50 * (1 + (int)(file_bytes_ / (512 * 1024)));
    if (last_slot_ - last_synced_slot_ >= interval) {
        last_synced_slot_ = last_slot_;
        flush();
    }
#endif
}

void Recorder::record_event(uint8_t code, uint32_t arg) {
    if (!have_keyframe_) return note_predawn_drop();
    if (over_size_cap()) return;
    uint8_t payload[5];
    payload[0] = code;
    memcpy(payload + 1, &arg, 4);
    append_record((uint32_t)(last_slot_ + 1), REC_EVENTS, payload,
                  sizeof(payload));
}

void Recorder::record_effect(uint8_t subtype, uint8_t player_idx,
                             const std::vector<uint8_t> &body) {
    if (!have_keyframe_) return note_predawn_drop();
    if (over_size_cap()) return;
    std::vector<uint8_t> payload;
    payload.reserve(2 + body.size());
    payload.push_back(subtype);
    payload.push_back(player_idx);
    payload.insert(payload.end(), body.begin(), body.end());
    // Stamped with the LAST state record's slot, not the next one's. An
    // effect stamped last_slot_ + 1 becomes due at the same playback
    // instant as that slot's state record and sits BEFORE it in the file,
    // so playback applied the effect and then the state rebuild in the
    // same poll batch — an FX_BULLET muzzle clone was destroyed before a
    // single draw, which nullified the whole feature. At last_slot_ the
    // effect follows the already-applied state record, spawns after its
    // rebuild, and survives until the next one — the online client's
    // MSG_SHOT visual, quantized to the start of the slot (≤100 ms early
    // rather than down-range and late). have_keyframe_ guarantees
    // last_slot_ >= 0 here.
    append_record((uint32_t)last_slot_, REC_EFFECT,
                  payload.empty() ? NULL : &payload[0], payload.size());
}

// True when bytes reached the file (the caller owns the web sync, so a
// flush syncs once for the chunk AND the header patch behind it).
bool Recorder::write_chunk() {
    if (chunk_.empty()) return false;
    FILE *fp = fopen(path_.c_str(), "ab");
    if (!fp) {
        // Keep the chunk for the next checkpoint — a transient failure
        // costs nothing. But a disk that never comes back must not grow
        // the RAM chunk for the rest of the run: checkpoints are minutes
        // apart, so after this many straight failures the recording is
        // dead (it could never be finalized either). The file keeps its
        // intact records + stale header — ordinary crash-artifact
        // semantics the reader already tolerates.
        if (++failed_writes_ >= MAX_FAILED_WRITES) {
            SDL_Log("replay: recording stopped (%d failed writes to %s)",
                    failed_writes_, path_.c_str());
            chunk_.clear();
            ok_ = false;
        }
        return false;
    }
    failed_writes_ = 0;
    // A short write (a full disk, a browser quota) leaves a truncated final
    // record. The reader drops that — but only ever as the LAST thing in the
    // file: everything appended after it sits behind the break where no
    // reader arrives, so ignoring the error meant the rest of the run was
    // recorded into a void while the header patch kept advertising the real
    // score and duration. Cut back to the last intact boundary (which we
    // know exactly: the file was this long before the append) and stop.
    size_t wrote = fwrite(&chunk_[0], 1, chunk_.size(), fp);
    bool wrote_ok = wrote == chunk_.size() && fflush(fp) == 0;
    if (fclose(fp) != 0) wrote_ok = false;
    if (!wrote_ok) {
        SDL_Log("replay: recording stopped (write of %u bytes to %s failed "
                "after %u) - the run so far stays playable",
                (unsigned)chunk_.size(), path_.c_str(), (unsigned)wrote);
        truncate_file(path_, (int64_t)header_.header_size + (int64_t)file_bytes_);
        chunk_.clear();
        // Leave an honest header over what survived, then stop: last_slot_
        // counted records this append was going to add, so re-read it from
        // the file rather than overstating the timeline.
        last_slot_ = last_record_slot(path_);
        patch_header_tail();
        ok_ = false;
        web_sync();
        return false;
    }
    file_bytes_ += chunk_.size();   // sizes the web sync interval and the cap
    chunk_.clear();
    return true;
}

// A flag var set to an explicitly-false value counts as unset:
// NEWTONIA_REPLAY_ENABLE=0 must not force recording ON (presence-only
// testing made it do exactly that — the Options row truthfully showed
// "ENV ON", but nobody writing =0 means on).
static bool env_flag_set(const char *name) {
    const char *v = SDL_getenv(name);
    if (!v) return false;
    return !(v[0] == '\0' || strcmp(v, "0") == 0 ||
             SDL_strcasecmp(v, "false") == 0 || SDL_strcasecmp(v, "off") == 0);
}

int recording_override() {
    if (env_flag_set("NEWTONIA_REPLAY_DISABLE")) return 0;  // disable wins
    if (env_flag_set("NEWTONIA_REPLAY_ENABLE")) return 1;
    return -1;
}

// Stop growing a recording that has outrun its storage, keeping everything
// recorded so far. Unbounded growth was an accepted limitation while
// recording was opt-in and desktop-shaped (REPLAY.md: "cap on file size for
// marathon runs — none locally"); default-ON on the web changes the stakes,
// because IndexedDB is an ORIGIN quota shared with savegame.dat,
// preferences.ini and stats.dat, and a browser under storage pressure
// evicts the origin as a unit. An unbounded replay can therefore cost the
// player their save, which no replay is worth. Native platforms write to a
// real filesystem and keep the old behaviour.
//
// The file stays perfectly playable — it simply ends where the cap fell,
// which is the same shape as any abandoned run.
bool Recorder::over_size_cap() {
#ifdef __EMSCRIPTEN__
    static const size_t CAP = 32u * 1024u * 1024u;  // ~2 h of play
    if (file_bytes_ + chunk_.size() < CAP) return false;
    if (!size_capped_) {
        size_capped_ = true;
        SDL_Log("replay: size cap reached (%u MB) - recording stops here, "
                "the run so far stays playable",
                (unsigned)(CAP / (1024u * 1024u)));
        write_chunk();       // bank what is already in RAM
        patch_header_tail(); // and leave an honest header behind it
        web_sync();
    }
    return true;
#else
    return false;
#endif
}

void Recorder::await_keyframe() { have_keyframe_ = false; }

void Recorder::flush() {
    if (!ok_) return;
    // Patch BEFORE the sync, not after. write_chunk schedules FS.syncfs and
    // the patch then mutates the same file; that only ever worked because
    // syncfs does its reading on a later turn of the event loop — an
    // ordering nothing stated or enforced. Doing the writes first and
    // syncing once at the end needs no such assumption, and covers the
    // empty-chunk case, where write_chunk returns before its own sync and
    // the patched tail used to sit in MEMFS until something else synced.
    bool wrote = write_chunk();
    if (patch_header_tail() || wrote) web_sync();
}

// Refresh the patchable tail (score / generation / duration) on every
// flush, not only at finalize.
//
// The header is written once at creation and patched at a clean stop, so
// any run that never reaches finalize — a crash, a killed tab — kept the
// creation values: score 0, generation 0, duration 0. That was invisible
// while such files were rare and unplayable; the web's interval flush
// makes them ordinary, and the REPLAYS list rendered a ten-level run as
// "SCORE 0  LEVEL 1" (field, 2026-07-29). The records were always right —
// only the summary lied.
//
// FLAG_CLEAN still belongs to finalize alone: it is what marks a run
// properly closed, and maybe_promote_best gates on it, so a crash artifact
// stays watchable-but-not-promotable exactly as before.
bool Recorder::patch_header_tail() {
    if (!ok_) return false;
    header_.duration_ms = (uint32_t)((last_slot_ + 1) * 100);
    FILE *fp = fopen(path_.c_str(), "r+b");
    if (!fp) return false;
    uint8_t buf[Header::SIZE];
    pack_header(buf, header_);
    bool wrote = false;
    if (fseek(fp, (long)Header::PATCH_OFFSET, SEEK_SET) == 0)
        wrote = fwrite(buf + Header::PATCH_OFFSET, 1,
                       Header::SIZE - Header::PATCH_OFFSET, fp) ==
                Header::SIZE - Header::PATCH_OFFSET;
    fclose(fp);
    return wrote;
}

// The caller owns the score/generation; the recorder just banks them so
// the next flush can write them out (the web's interval flush has no other
// way to learn them).
void Recorder::note_progress(uint32_t score, uint32_t generation) {
    if (score > header_.final_score) header_.final_score = score;
    header_.generation = generation;
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
        // ended with a resumed file: the run is still over — retire what the
        // earlier sessions recorded.
        if (resumed_ && ended) retire();
        return;
    }

    write_chunk();

    header_.flags = (uint8_t)((cheated ? FLAG_CHEATED : 0) | FLAG_CLEAN |
                              (ended ? FLAG_ENDED : 0));
    if (player_count > header_.player_count) header_.player_count = player_count;
    // note_progress banks the running max; the caller's figure is the same
    // by construction today, but the bank exists because a caller's value
    // can be stale, so keep the higher of the two rather than assuming.
    if (score > header_.final_score) header_.final_score = score;
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

    if (ended) retire();
}

// A truly-over run leaves its live slot. Offline that is a rotation:
// current.nrp -> recent (+best check). Online there is nowhere to go —
// online.nrp IS the listed ONLINE RUN slot, overwritten per session like
// recent is per offline run — so retirement is just the best check.
void Recorder::retire() {
    if (path_ == online_path()) best_check_online();
    else rotate_to_recent(path_);
}

}  // namespace Replay
