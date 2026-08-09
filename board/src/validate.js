// Server-side validation of a submitted replay blob (LEADERBOARD.md L1).
//
// This is the worker's port of the game's own reader checks (replay.cpp):
// parse the 64-byte header, then walk the record framing without touching
// payload contents. Payload bytes stay unvalidated on purpose — the game's
// hardened reader (REPLAY.md R4 ingest work) is what parses them at watch
// time; the worker only needs to know the file is a structurally sound
// recording whose header tells the truth about being a clean, non-cheated
// run. Everything here is pure (no bindings, no crypto) so the unit test
// runs under plain node.
//
// Header layout (little-endian, replay.h `Replay::Header`):
//   0  u32 magic "NWRP"          32 u64 run_id
//   4  u16 format_version        40 u64 date
//   6  u16 header_size           48 u8  flags
//   8  char[24] game_version     49 u8  player_count
//                                50 u16 save_version
//                                52 u32 final_score
//                                56 u32 generation
//                                60 u32 duration_ms
// Records at header_size: [u32 slot | u8 kind | u32 len | payload] ...

export const MAGIC = 0x5052574e; // "NWRP"
// Accepted format range — the server mirrors the game's FORMAT_VERSION /
// MIN_FORMAT_VERSION pair and must move with them: a release that bumps the
// game's format needs this raised in the same season it starts submitting.
export const FORMAT_MIN = 1;
export const FORMAT_MAX = 2;
export const HEADER_FIXED = 64; // v1/v2 on-disk header size
export const HEADER_SIZE_MAX = 4096; // reader accepts 64..4096 (REPLAY.md R4)
export const GAME_VERSION_LEN = 24;

export const FLAG_CHEATED = 1;
export const FLAG_CLEAN = 2;
export const FLAG_ENDED = 4;

export const REC_KEYFRAME = 1;
export const REC_DELTA = 2;
export const REC_EVENTS = 3;
export const REC_EFFECT = 4;

// The game's own read-side bounds (replay.cpp): mirror them exactly so the
// server never accepts a file the game would refuse to play.
export const MAX_RECORD_SLOT = 20 * 1000 * 1000;
export const MAX_RECORD_BYTES = 8 * 1024 * 1024;

// Submission size window. Max is the web recorder's cap (~2 h of play,
// LEADERBOARD.md); min is a header plus one record head — anything smaller
// cannot possibly contain a run.
export const MAX_SUBMISSION_BYTES = 32 * 1024 * 1024;
export const MIN_SUBMISSION_BYTES = HEADER_FIXED + 9;

// Largest player_count a run may carry (FOURPLAYER.md D10): 3–4 player
// runs are admitted, and the worker keys every co-op count (>= 2) onto
// the single players=2 co-op board.
export const MAX_PLAYERS = 4;

// The season key is the header's game_version, verbatim (LEADERBOARD.md).
// Bound it to printable non-space ASCII so it is safe as a D1 key, an R2
// key segment, and a log token without any escaping downstream.
export function season_ok(s) {
  if (typeof s !== "string" || s.length < 1 || s.length > GAME_VERSION_LEN - 1)
    return false;
  return [...s].every((c) => {
    const b = c.charCodeAt(0);
    return b > 0x20 && b < 0x7f && c !== "/" && c !== "\\";
  });
}

// Parse and sanity-check the header. Returns {ok:false, reason} or
// {ok:true, header:{format_version, header_size, season, run_id (decimal
// string), date, flags, player_count, save_version, score, generation,
// duration_ms}}.
export function parse_header(buf) {
  if (!(buf instanceof Uint8Array)) return { ok: false, reason: "bad-buffer" };
  if (buf.length < HEADER_FIXED) return { ok: false, reason: "too-small" };
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  if (dv.getUint32(0, true) !== MAGIC) return { ok: false, reason: "bad-magic" };
  const format_version = dv.getUint16(4, true);
  if (format_version < FORMAT_MIN) return { ok: false, reason: "format-too-old" };
  if (format_version > FORMAT_MAX) return { ok: false, reason: "format-too-new" };
  const header_size = dv.getUint16(6, true);
  if (header_size < HEADER_FIXED || header_size > HEADER_SIZE_MAX ||
      header_size > buf.length)
    return { ok: false, reason: "bad-header" };
  let season = "";
  for (let i = 0; i < GAME_VERSION_LEN; i++) {
    const b = buf[8 + i];
    if (b === 0) break;
    season += String.fromCharCode(b);
  }
  if (!season_ok(season)) return { ok: false, reason: "bad-season" };
  const run_id = dv.getBigUint64(32, true);
  if (run_id === 0n) return { ok: false, reason: "bad-run-id" };
  return {
    ok: true,
    header: {
      format_version,
      header_size,
      season,
      run_id: run_id.toString(10),
      date: Number(dv.getBigUint64(40, true)),
      flags: dv.getUint8(48),
      player_count: dv.getUint8(49),
      save_version: dv.getUint16(50, true),
      score: dv.getUint32(52, true),
      generation: dv.getUint32(56, true),
      duration_ms: dv.getUint32(60, true),
    },
  };
}

// Walk the record framing from header_size to EOF. Returns {ok:false,
// reason} or {ok:true, records, keyframes, deltas, last_slot, truncated}.
//
// Mirrors the game reader's tolerance: a truncated FINAL record (crash
// artifact / mid-append cut) ends the walk cleanly. Mirrors its bounds:
// slot and len are capped, and the FIRST record must be a keyframe (the
// reader rejects "no leading keyframe" outright). Slots are NOT required
// to be monotonic: events stamp the upcoming slot while effects stamp the
// last emitted one, so a legal file can interleave slot N+1 then N within
// one inter-slot window — the game's reader doesn't order-check either.
export function walk_records(buf, header_size) {
  let pos = header_size;
  let records = 0, keyframes = 0, deltas = 0, last_slot = -1;
  let truncated = false;
  while (pos + 9 <= buf.length) {
    const dv = new DataView(buf.buffer, buf.byteOffset + pos, 9);
    const slot = dv.getUint32(0, true);
    const kind = dv.getUint8(4);
    const len = dv.getUint32(5, true);
    if (slot > MAX_RECORD_SLOT || len > MAX_RECORD_BYTES)
      return { ok: false, reason: "bad-record" };
    if (kind < REC_KEYFRAME || kind > REC_EFFECT)
      return { ok: false, reason: "bad-record" };
    if (pos + 9 + len > buf.length) { truncated = true; break; }
    if (records === 0 && kind !== REC_KEYFRAME)
      return { ok: false, reason: "no-leading-keyframe" };
    records++;
    if (kind === REC_KEYFRAME) keyframes++;
    if (kind === REC_DELTA) deltas++;
    if (slot > last_slot) last_slot = slot;
    pos += 9 + len;
  }
  if (pos < buf.length && pos + 9 > buf.length) truncated = true;
  if (records === 0) return { ok: false, reason: "no-records" };
  return { ok: true, records, keyframes, deltas, last_slot, truncated };
}

// Does the header's claimed duration agree with the records actually
// present? Returns null when it does (or when the disagreement is explained)
// and a short human-readable note otherwise. OBSERVATION ONLY — the caller
// logs it and admits the submission regardless. See LEADERBOARD.md S3.
//
// The invariant is exact, not statistical: the recorder derives the field as
// `duration_ms = (last_slot_ + 1) * 100` (replay.cpp), and every drop path
// (pre-keyframe hold across an online rejoin seam, size cap, failed write)
// returns BEFORE the `last_slot_++`, so held-out records never advance the
// timeline and the slot line stays dense. The one legal wobble upward is
// REC_EVENTS, which stamps the UPCOMING slot (`last_slot_ + 1`), so a file
// whose final record is an event carries a max slot one above the header's.
//
// It is deliberately NOT an admission gate, because legitimate files can
// still fall outside it: the header is patched at each clean stop and
// recording continues afterwards, so a process that dies before the next
// patch leaves a stale header over newer records — and that file still
// carries FLAG_CLEAN, so maybe_promote_best will happily promote it. A
// player losing a real run to a corner like that is a worse outcome than
// the fabricated file this would have caught, especially since the check is
// no defence at all against the actual threat: `final_score` has no
// structural correlate here, so a hex-edited score leaves a file that
// satisfies every bound in this module (L5/L6 remain the answer to that).
export function shape_note(hd, w) {
  if (hd.duration_ms % 100 !== 0)
    return `duration ${hd.duration_ms}ms is not a whole slot`;
  const slots = hd.duration_ms / 100;  // last_slot + 1 at patch time
  const carried = w.last_slot + 1;
  if (carried === slots || carried === slots + 1) return null;  // exact, or events
  if (carried < slots)
    return w.truncated
        ? null  // tail cut mid-record: the reader tolerates it, so do we
        : `header claims ${slots} slot(s), file carries ${carried}`;
  return `file carries ${carried} slot(s), header claims ${slots}`;
}

// Full submission check: header + admission flags + record walk. The
// header is the source of truth for everything the row stores (score,
// season, players, run_id) — there is no separately claimed score to
// cross-check, by design.
export function validate_submission(buf) {
  if (!(buf instanceof Uint8Array)) return { ok: false, reason: "bad-buffer" };
  if (buf.length < MIN_SUBMISSION_BYTES) return { ok: false, reason: "too-small" };
  if (buf.length > MAX_SUBMISSION_BYTES) return { ok: false, reason: "too-large" };
  const h = parse_header(buf);
  if (!h.ok) return h;
  const hd = h.header;
  if (hd.flags & FLAG_CHEATED) return { ok: false, reason: "cheated" };
  if (!(hd.flags & FLAG_CLEAN)) return { ok: false, reason: "not-clean" };
  if (hd.score === 0) return { ok: false, reason: "zero-score" };
  if (hd.player_count < 1 || hd.player_count > MAX_PLAYERS)
    return { ok: false, reason: "bad-players" };
  const w = walk_records(buf, hd.header_size);
  if (!w.ok) return w;
  if (w.deltas === 0) return { ok: false, reason: "no-delta" };
  return { ok: true, header: hd, stats: w };
}
