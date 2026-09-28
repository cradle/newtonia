// Unit test for cloud_sync_merge.h — the rules deciding which copy of the
// save, stats and high score wins when a device syncs (cloud_sync.h).
// Links nothing:
//
//   g++ -std=c++11 -Wall -I. test/unit/cloud_sync_merge_test.cpp -o /tmp/cloud_sync_merge_test && /tmp/cloud_sync_merge_test

#include <cstdio>
#include <cstring>

#include "../../cloud_sync_merge.h"

using namespace CloudMerge;

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { fails++; \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
  // ── high score ──
  {
    int32_t v = 0;
    CHECK(parse_highscore(highscore_bytes(12345), v) && v == 12345);
    CHECK(!parse_highscore(std::string("ab"), v));
  }

  // ── stats: round trip, v1-length file, max + mask OR ──
  {
    std::vector<uint32_t> a = {100, 0x05, 900, 3, 7, 2, 11, 3600, 40, 1};
    std::vector<uint32_t> back;
    CHECK(parse_stats(stats_bytes(a), back) && back == a);

    // The disk stamp stays 1 (stats.cpp ST_DISK_VERSION).
    std::string b = stats_bytes(a);
    uint16_t ver = 0;
    std::memcpy(&ver, b.data() + 4, 2);
    CHECK(ver == 1);

    // A v1 file from an old install: kills + mask only.
    std::vector<uint32_t> old = {250, 0x12};
    std::vector<uint32_t> m = merge_stats(a, old);
    CHECK(m.size() == a.size());
    CHECK(m[0] == 250);            // the larger kill count
    CHECK(m[1] == (0x05 | 0x12));  // mask OR'd, not max'd
    CHECK(m[2] == 900);            // fields the short file lacks carry over
    CHECK(merge_stats(old, a) == m);  // order never matters

    CHECK(!parse_stats(std::string("NOPE!!"), back));
    std::string bad = stats_bytes(a);
    bad[0] ^= 1;
    CHECK(!parse_stats(bad, back));
  }

  // ── savegame envelope ──
  {
    SaveRecord r;
    r.stamp = 1759050000123ULL;
    r.present = true;
    r.bytes = std::string("NWTN\x17\x00payload", 13);
    SaveRecord back;
    CHECK(parse_save_record(save_record_bytes(r), back));
    CHECK(back.stamp == r.stamp && back.present && back.bytes == r.bytes);

    SaveRecord gone;
    gone.stamp = 5;
    CHECK(parse_save_record(save_record_bytes(gone), back));
    CHECK(back.stamp == 5 && !back.present && back.bytes.empty());

    // Present but too short to hold a header: damage, rejected.
    std::string stub = save_record_bytes(gone);
    stub[8] = 1;
    stub += "NW";
    CHECK(!parse_save_record(stub, back));
    CHECK(!parse_save_record(std::string("short"), back));
  }

  // ── newest save wins, deletes included ──
  {
    CHECK(decide_save(0, false, 0) == SAVE_NONE);    // nothing anywhere
    CHECK(decide_save(10, false, 0) == SAVE_PUSH);   // first sync of a save
    CHECK(decide_save(0, true, 10) == SAVE_PULL);    // a new phone
    CHECK(decide_save(20, true, 10) == SAVE_PUSH);   // this device is newer
    CHECK(decide_save(10, true, 20) == SAVE_PULL);   // the other is newer
    CHECK(decide_save(10, true, 10) == SAVE_NONE);   // already in step
  }

  if (fails) {
    std::printf("%d check(s) failed\n", fails);
    return 1;
  }
  std::printf("cloud_sync_merge_test: all checks passed\n");
  return 0;
}
