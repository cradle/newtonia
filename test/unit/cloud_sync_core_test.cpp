// Unit test for cloud_sync_core.cpp — the sync driver both cloud backends
// share — against a fake Store and a scratch directory. Run through
// test/unit/cloud_sync_core.sh (needs the SDL2 development package).

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../cloud_sync.h"
#include "../../cloud_sync_core.h"
#include "../../cloud_sync_merge.h"
#include "../../savegame.h"

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { fails++; \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct FakeStore : CloudCore::Store {
  std::string val[3];
  bool has[3] = {false, false, false};
  bool stamp_known = false;
  uint64_t stamp = 0;
  bool read(int f, std::string &out) override {
    out = val[f];
    return has[f];
  }
  void write(int f, const std::string &b) override { val[f] = b; has[f] = true; }
  bool load_save_stamp(uint64_t &out) override {
    out = stamp;
    return stamp_known;
  }
  void store_save_stamp(uint64_t s) override { stamp = s; stamp_known = true; }
};

static std::string g_dir;

static std::string path(int f) { return CloudCore::local_path(f); }

static void put(int f, const std::string &b) {
  FILE *fp = fopen(path(f).c_str(), "wb");
  fwrite(b.data(), 1, b.size(), fp);
  fclose(fp);
}

static bool get(int f, std::string &out) {
  out.clear();
  FILE *fp = fopen(path(f).c_str(), "rb");
  if (!fp) return false;
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
  fclose(fp);
  return true;
}

static void clear_local() {
  for (int f = 0; f < 3; f++) std::remove(path(f).c_str());
}

static std::string save_bytes(const char *payload) {
  std::string b(6, '\0');
  uint32_t magic = Save::GameState::MAGIC;
  std::memcpy(&b[0], &magic, 4);
  return b + payload;
}

static std::string record(uint64_t stamp, const std::string *bytes) {
  CloudMerge::SaveRecord r;
  r.stamp = stamp;
  r.present = bytes != NULL;
  if (bytes) r.bytes = *bytes;
  return CloudMerge::save_record_bytes(r);
}

int main() {
  char tmpl[] = "/tmp/cloud_sync_core.XXXXXX";
  if (!mkdtemp(tmpl)) return 2;
  g_dir = std::string(tmpl) + "/";
  CloudCore::set_local_dir_for_test(g_dir);

  // ── a fresh install pulls everything the cloud holds ──
  {
    clear_local();
    FakeStore s;
    std::string save = save_bytes("run");
    s.write(CloudSync::SAVEGAME, record(500, &save));
    s.write(CloudSync::STATS, CloudMerge::stats_bytes({40, 0x3, 7}));
    s.write(CloudSync::HIGHSCORE, CloudMerge::highscore_bytes(9000));
    unsigned c = CloudCore::sync_files(s);
    CHECK(c == 7u);
    std::string b;
    CHECK(get(CloudSync::SAVEGAME, b) && b == save);
    CHECK(s.stamp_known && s.stamp == 500);
    std::vector<uint32_t> st;
    CHECK(get(CloudSync::STATS, b) && CloudMerge::parse_stats(b, st) &&
          st == std::vector<uint32_t>({40, 0x3, 7}));
    int32_t hs = 0;
    CHECK(get(CloudSync::HIGHSCORE, b) && CloudMerge::parse_highscore(b, hs) && hs == 9000);
    // Nothing left to do on a second pass.
    CHECK(CloudCore::sync_files(s) == 0u);
  }

  // ── both sides played: stats merge both ways, the higher score and the
  // newer save win ──
  {
    clear_local();
    FakeStore s;
    put(CloudSync::STATS, CloudMerge::stats_bytes({100, 0x1, 2}));
    put(CloudSync::HIGHSCORE, CloudMerge::highscore_bytes(700));
    std::string mine = save_bytes("mine");
    put(CloudSync::SAVEGAME, mine);
    s.store_save_stamp(900);
    std::string theirs = save_bytes("theirs");
    s.val[CloudSync::SAVEGAME] = record(800, &theirs);
    s.has[CloudSync::SAVEGAME] = true;
    s.write(CloudSync::STATS, CloudMerge::stats_bytes({60, 0x4, 5}));
    s.write(CloudSync::HIGHSCORE, CloudMerge::highscore_bytes(600));
    unsigned c = CloudCore::sync_files(s);
    CHECK(c == (1u << CloudSync::STATS));
    std::vector<uint32_t> want = {100, 0x5, 5}, st;
    std::string b;
    CHECK(get(CloudSync::STATS, b) && CloudMerge::parse_stats(b, st) && st == want);
    CHECK(CloudMerge::parse_stats(s.val[CloudSync::STATS], st) && st == want);
    int32_t hs = 0;
    CHECK(CloudMerge::parse_highscore(s.val[CloudSync::HIGHSCORE], hs) && hs == 700);
    CloudMerge::SaveRecord r;
    CHECK(CloudMerge::parse_save_record(s.val[CloudSync::SAVEGAME], r) &&
          r.stamp == 900 && r.present && r.bytes == mine);
    CHECK(get(CloudSync::SAVEGAME, b) && b == mine);
  }

  // ── a game over elsewhere ends the run here ──
  {
    clear_local();
    FakeStore s;
    put(CloudSync::SAVEGAME, save_bytes("old"));
    s.store_save_stamp(100);
    s.write(CloudSync::SAVEGAME, record(200, NULL));
    CHECK(CloudCore::sync_save(s));
    std::string b;
    CHECK(!get(CloudSync::SAVEGAME, b));
    CHECK(s.stamp == 200);
  }

  // ── ...and a removal that fails keeps the old stamp, so it retries ──
  {
    clear_local();
    FakeStore s;
    // A non-empty directory where the save should be: remove() fails.
    std::string p = path(CloudSync::SAVEGAME);
    mkdir(p.c_str(), 0700);
    std::string inner = p + "/x";
    FILE *fp = fopen(inner.c_str(), "wb");
    fclose(fp);
    s.store_save_stamp(100);
    s.write(CloudSync::SAVEGAME, record(200, NULL));
    CHECK(!CloudCore::sync_save(s));
    CHECK(s.stamp == 100);
    std::remove(inner.c_str());
    rmdir(p.c_str());
    CHECK(CloudCore::sync_save(s));  // the retry lands
    CHECK(s.stamp == 200);
  }

  // ── a cloud save that isn't a save is never landed ──
  {
    clear_local();
    FakeStore s;
    std::string junk = "JUNKxxxxxx";
    s.write(CloudSync::SAVEGAME, record(300, &junk));
    CHECK(!CloudCore::sync_save(s));
    std::string b;
    CHECK(!get(CloudSync::SAVEGAME, b));
  }

  // ── a local write stamps now and pushes ──
  {
    clear_local();
    FakeStore s;
    std::string mine = save_bytes("fresh");
    put(CloudSync::SAVEGAME, mine);
    uint64_t before = CloudCore::now_ms();
    CloudCore::save_written(s);
    CloudMerge::SaveRecord r;
    CHECK(CloudMerge::parse_save_record(s.val[CloudSync::SAVEGAME], r) &&
          r.present && r.bytes == mine && r.stamp >= before && s.stamp == r.stamp);
    // Deleting pushes a tombstone.
    std::remove(path(CloudSync::SAVEGAME).c_str());
    CloudCore::save_written(s);
    CHECK(CloudMerge::parse_save_record(s.val[CloudSync::SAVEGAME], r) && !r.present);
  }

  clear_local();
  rmdir(tmpl);
  if (fails) {
    std::printf("%d check(s) failed\n", fails);
    return 1;
  }
  std::printf("cloud_sync_core_test: all checks passed\n");
  return 0;
}
