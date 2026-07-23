#!/usr/bin/env python3
"""Parse a .nrp replay file (REPLAY.md R1 format) and print header fields +
record counts as shell-evalable key=value lines. Used by replay.sh.

Tolerates a truncated final record (crash artifact) — reported as
truncated_tail=1, records before it still count — and a header staler than
the records (that's just what the fields say).

Usage: replay_check.py <file.nrp>
Exit 0 with fields on stdout; exit 1 (with a message) on missing file / bad
magic / bad format version.
"""
import struct
import sys

MAGIC = 0x5052574E  # "NWRP"
HEADER_SIZE = 64
KINDS = {1: "keyframes", 2: "deltas", 3: "events", 4: "effects"}


def main():
    if len(sys.argv) != 2:
        print("usage: replay_check.py <file.nrp>", file=sys.stderr)
        return 1
    try:
        data = open(sys.argv[1], "rb").read()
    except OSError as e:
        print(f"unreadable: {e}", file=sys.stderr)
        return 1
    if len(data) < HEADER_SIZE:
        print("too short for a header", file=sys.stderr)
        return 1

    (magic, fmt, hsize) = struct.unpack_from("<IHH", data, 0)
    if magic != MAGIC:
        print("bad magic", file=sys.stderr)
        return 1
    if not (1 <= fmt <= 1) or not (HEADER_SIZE <= hsize <= 4096):
        print(f"bad format/header_size: {fmt}/{hsize}", file=sys.stderr)
        return 1
    game_version = data[8:32].split(b"\0")[0].decode("ascii", "replace")
    (run_id, date) = struct.unpack_from("<QQ", data, 32)
    flags, players = data[48], data[49]
    (score, gen, dur) = struct.unpack_from("<III", data, 52)

    counts = {"keyframes": 0, "deltas": 0, "events": 0, "effects": 0}
    last_slot = -1
    first_kind = ""
    truncated = 0
    pos = hsize
    n = 0
    while pos + 9 <= len(data):
        (slot,) = struct.unpack_from("<I", data, pos)
        kind = data[pos + 4]
        (length,) = struct.unpack_from("<I", data, pos + 5)
        if length > 8 * 1024 * 1024 or pos + 9 + length > len(data):
            truncated = 1
            break
        name = KINDS.get(kind)
        if name is None:
            truncated = 1  # unknown kind: treat as corruption, stop
            break
        counts[name] += 1
        if slot > last_slot:
            last_slot = slot
        if n == 0:
            first_kind = name
        n += 1
        pos += 9 + length
    if not truncated and pos != len(data):
        truncated = 1  # trailing partial record framing

    print(f"run_id={run_id:016x}")
    print(f"date={date}")
    print(f"game_version={game_version}")
    print(f"flags={flags}")
    print(f"cheated={1 if flags & 1 else 0}")
    print(f"clean={1 if flags & 2 else 0}")
    print(f"ended={1 if flags & 4 else 0}")
    print(f"players={players}")
    print(f"score={score}")
    print(f"generation={gen}")
    print(f"duration_ms={dur}")
    print(f"records={n}")
    print(f"keyframes={counts['keyframes']}")
    print(f"deltas={counts['deltas']}")
    print(f"events={counts['events']}")
    print(f"effects={counts['effects']}")
    print(f"last_slot={last_slot}")
    print(f"first_kind={first_kind}")
    print(f"truncated_tail={truncated}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
