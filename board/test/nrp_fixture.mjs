// Synthetic .nrp builder for the board tests: emits the same header and
// record framing replay.cpp writes (little-endian), with every field
// overridable so each test can break exactly one thing.
import {
  MAGIC, FLAG_CLEAN, REC_KEYFRAME, REC_DELTA, REC_EVENTS,
} from "../src/validate.js";

export function build_header(over = {}) {
  const h = {
    magic: MAGIC, format_version: 2, header_size: 64,
    game_version: "v1.2.3", run_id: 12345678901n, date: 1753900000n,
    flags: FLAG_CLEAN, player_count: 1, save_version: 17,
    score: 4200, generation: 7, duration_ms: 300000,
    ...over,
  };
  const buf = new Uint8Array(64);
  const dv = new DataView(buf.buffer);
  dv.setUint32(0, h.magic, true);
  dv.setUint16(4, h.format_version, true);
  dv.setUint16(6, h.header_size, true);
  for (let i = 0; i < Math.min(h.game_version.length, 24); i++)
    buf[8 + i] = h.game_version.charCodeAt(i);
  dv.setBigUint64(32, BigInt(h.run_id), true);
  dv.setBigUint64(40, BigInt(h.date), true);
  dv.setUint8(48, h.flags);
  dv.setUint8(49, h.player_count);
  dv.setUint16(50, h.save_version, true);
  dv.setUint32(52, h.score, true);
  dv.setUint32(56, h.generation, true);
  dv.setUint32(60, h.duration_ms, true);
  return buf;
}

export function build_record(slot, kind, payload_len) {
  const buf = new Uint8Array(9 + payload_len);
  const dv = new DataView(buf.buffer);
  dv.setUint32(0, slot, true);
  dv.setUint8(4, kind);
  dv.setUint32(5, payload_len, true);
  for (let i = 0; i < payload_len; i++) buf[9 + i] = (slot + i) & 0xff;
  return buf;
}

export function concat(...parts) {
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let off = 0;
  for (const p of parts) { out.set(p, off); off += p.length; }
  return out;
}

// A structurally valid little run: keyframe, deltas, an event.
export function build_nrp(header_over = {}, records = null) {
  const recs = records || [
    build_record(0, REC_KEYFRAME, 200),
    build_record(1, REC_DELTA, 40),
    build_record(2, REC_EVENTS, 5),
    build_record(2, REC_DELTA, 40),
  ];
  return concat(build_header(header_over), ...recs);
}
