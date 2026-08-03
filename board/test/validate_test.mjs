// Unit test for the submission validator (LEADERBOARD.md L1): the worker's
// port of the game's header/record-framing checks. Pure node, no wrangler.
// Run: node test/validate_test.mjs
import {
  parse_header, walk_records, validate_submission, season_ok, shape_note,
  FLAG_CHEATED, FLAG_CLEAN, FLAG_ENDED,
  REC_KEYFRAME, REC_DELTA, REC_EFFECT, REC_EVENTS,
  MAX_RECORD_SLOT, MAX_SUBMISSION_BYTES,
} from "../src/validate.js";
import {
  build_header, build_record, build_nrp, concat,
} from "./nrp_fixture.mjs";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}

// ---- season_ok ----
eq("plain tag ok", season_ok("v1.2.3"), true);
eq("describe tag ok", season_ok("v1.2.3-4-gabc1234+"), true);
eq("empty rejected", season_ok(""), false);
eq("24 chars rejected", season_ok("x".repeat(24)), false);
eq("23 chars ok", season_ok("x".repeat(23)), true);
eq("space rejected", season_ok("v1 .2"), false);
eq("slash rejected", season_ok("v1/2"), false);
eq("control rejected", season_ok("v1\x07"), false);

// ---- parse_header ----
{
  const h = parse_header(build_header());
  eq("header parses", h.ok, true);
  eq("season extracted", h.header.season, "v1.2.3");
  eq("run_id decimal string", h.header.run_id, "12345678901");
  eq("score", h.header.score, 4200);
  eq("generation", h.header.generation, 7);
  eq("players", h.header.player_count, 1);
}
eq("bad magic", parse_header(build_header({ magic: 0x1234 })).reason, "bad-magic");
eq("format too new", parse_header(build_header({ format_version: 3 })).reason,
   "format-too-new");
eq("format too old", parse_header(build_header({ format_version: 0 })).reason,
   "format-too-old");
eq("zero run_id", parse_header(build_header({ run_id: 0n })).reason, "bad-run-id");
eq("short buffer", parse_header(build_header().slice(0, 63)).reason, "too-small");
eq("header_size below 64",
   parse_header(build_nrp({ header_size: 32 })).reason, "bad-header");
{
  // A LONGER header (a future v3 with extra fields) is legal as long as
  // records start where header_size says — the reader accepts 64..4096.
  const pad = new Uint8Array(16);
  const file = concat(build_header({ header_size: 80 }), pad,
                      build_record(0, REC_KEYFRAME, 10),
                      build_record(1, REC_DELTA, 10));
  eq("longer header accepted", validate_submission(file).ok, true);
}

// ---- walk_records ----
{
  const w = walk_records(build_nrp(), 64);
  eq("walk ok", w.ok, true);
  eq("records counted", w.records, 4);
  eq("deltas counted", w.deltas, 2);
  eq("keyframes counted", w.keyframes, 1);
  eq("last slot", w.last_slot, 2);
  eq("not truncated", w.truncated, false);
}
{
  // Truncated final record (crash artifact): tolerated, like the game.
  const full = build_nrp();
  const w = walk_records(full.slice(0, full.length - 10), 64);
  eq("truncated tail tolerated", w.ok, true);
  eq("truncated flag set", w.truncated, true);
}
{
  // Delta first: the game's reader rejects "no leading keyframe".
  const file = build_nrp({}, [build_record(0, REC_DELTA, 10)]);
  eq("delta-first rejected", validate_submission(file).reason,
     "no-leading-keyframe");
}
{
  const file = build_nrp({}, [
    build_record(0, REC_KEYFRAME, 10),
    build_record(MAX_RECORD_SLOT + 1, REC_DELTA, 10),
  ]);
  eq("slot bound enforced", validate_submission(file).reason, "bad-record");
}
{
  const file = build_nrp({}, [
    build_record(0, REC_KEYFRAME, 10),
    build_record(1, 9, 10), // unknown kind
  ]);
  eq("unknown kind rejected", validate_submission(file).reason, "bad-record");
}
{
  // Effects stamp the LAST emitted slot while events stamp the upcoming
  // one, so slot order can legally regress within a window (validate.js).
  const file = build_nrp({}, [
    build_record(0, REC_KEYFRAME, 10),
    build_record(1, REC_DELTA, 10),
    build_record(2, REC_DELTA, 10), // events for slot 2 then...
    build_record(1, REC_EFFECT, 10), // ...an effect on last-emitted 1
  ]);
  eq("non-monotonic slots accepted", validate_submission(file).ok, true);
}

// ---- validate_submission admission flags ----
eq("clean run accepted", validate_submission(build_nrp()).ok, true);
eq("cheated rejected",
   validate_submission(build_nrp({ flags: FLAG_CLEAN | FLAG_CHEATED })).reason,
   "cheated");
eq("unclean rejected",
   validate_submission(build_nrp({ flags: 0 })).reason, "not-clean");
eq("ended not required",
   validate_submission(build_nrp({ flags: FLAG_CLEAN | FLAG_ENDED })).ok, true);
eq("zero score rejected",
   validate_submission(build_nrp({ score: 0 })).reason, "zero-score");
eq("bad player count",
   validate_submission(build_nrp({ player_count: 3 })).reason, "bad-players");
eq("two players accepted",
   validate_submission(build_nrp({ player_count: 2 })).ok, true);
{
  // No delta at all (zero-tick rule twin): rejected.
  const file = build_nrp({}, [build_record(0, REC_KEYFRAME, 10)]);
  eq("keyframe-only rejected", validate_submission(file).reason, "no-delta");
}
eq("oversize rejected",
   validate_submission(new Uint8Array(MAX_SUBMISSION_BYTES + 1)).reason,
   "too-large");
eq("undersize rejected",
   validate_submission(new Uint8Array(64)).reason, "too-small");

// ---- shape_note (LEADERBOARD.md S3: observation only, never a rejection) ----
{
  // The recorder derives duration_ms = (last_slot + 1) * 100, so a file whose
  // records end at slot 3 claims 400 ms.
  const recs = [
    build_record(0, REC_KEYFRAME, 20), build_record(1, REC_DELTA, 8),
    build_record(2, REC_DELTA, 8), build_record(3, REC_DELTA, 8),
  ];
  let v = validate_submission(concat(build_header({ duration_ms: 400 }), ...recs));
  eq("consistent file validates", v.ok, true);
  eq("consistent file has no shape note", shape_note(v.header, v.stats), null);

  // REC_EVENTS stamps the UPCOMING slot, so a file ending on one legally
  // carries a max slot above the header's count.
  v = validate_submission(concat(build_header({ duration_ms: 400 }), ...recs,
                                 build_record(4, REC_EVENTS, 5)));
  eq("trailing event is not a mismatch", shape_note(v.header, v.stats), null);

  // A fabricated run: four records claiming half an hour. NOTED, not refused.
  v = validate_submission(concat(build_header({ duration_ms: 1800000 }), ...recs));
  eq("fabricated duration still ADMITTED", v.ok, true);
  eq("fabricated duration noted", shape_note(v.header, v.stats),
     "header claims 18000 slot(s), file carries 4");

  // A stale header over newer records — the crash artifact that made this
  // unenforceable. Noted the other way round, and still admitted.
  v = validate_submission(concat(build_header({ duration_ms: 200 }), ...recs));
  eq("stale header still ADMITTED", v.ok, true);
  eq("stale header noted", shape_note(v.header, v.stats),
     "file carries 4 slot(s), header claims 2");

  // A duration that is not a whole slot cannot have come from the recorder.
  v = validate_submission(concat(build_header({ duration_ms: 450 }), ...recs));
  eq("part-slot duration noted", shape_note(v.header, v.stats),
     "duration 450ms is not a whole slot");
}

process.exit(failures ? 1 : 0);
