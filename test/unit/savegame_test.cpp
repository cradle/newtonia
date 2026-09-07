// Linux fault-injection test: GNU ld wrappers exercise real serialization
// without adding test switches to the game's persistence code.
#include "savegame.h"
#include <SDL.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

enum Fault { None, Open, Write, Close, FullDisk, Replace };
static Fault fault = None;
static int syncs = 0;
static int closes = 0;
void web_fs_sync(const char *) { ++syncs; }

extern "C" FILE *__real_fopen(const char *, const char *);
extern "C" size_t __real_fwrite(const void *, size_t, size_t, FILE *);
extern "C" int __real_fclose(FILE *);
extern "C" int __real_rename(const char *, const char *);

extern "C" FILE *__wrap_fopen(const char *path, const char *mode) {
    if (fault == Open) { errno = EACCES; return nullptr; }
    if (fault == FullDisk) {
        FILE *fp = __real_fopen("/dev/full", mode);
        // Ensure the small fixture's writes defer ENOSPC until fclose.
        static char buffer[8192];
        if (fp) setvbuf(fp, buffer, _IOFBF, sizeof(buffer));
        return fp;
    }
    return __real_fopen(path, mode);
}
extern "C" size_t __wrap_fwrite(const void *p, size_t size, size_t n, FILE *fp) {
    if (fault == Write) { errno = ENOSPC; return 0; }
    return __real_fwrite(p, size, n, fp);
}
extern "C" int __wrap_fclose(FILE *fp) {
    ++closes;
    const int result = __real_fclose(fp);
    if (fault == Close) { errno = EIO; return EOF; }
    return result;
}
extern "C" int __wrap_rename(const char *from, const char *to) {
    if (fault == Replace) { errno = EACCES; return -1; }
    return __real_rename(from, to);
}

static std::string contents(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

int main() {
    char root[] = "/tmp/newtonia-save-test.XXXXXX";
    assert(mkdtemp(root));
    assert(setenv("XDG_DATA_HOME", root, 1) == 0);
    char *pref = SDL_GetPrefPath("cc.gfm", "newtonia");
    assert(pref);
    const std::string dir(pref);
    SDL_free(pref);

    for (int online = 0; online != 2; ++online) {
        auto save = online ? Save::online_save_game : Save::save_game;
        auto load = online ? Save::online_load_game : Save::load_game;
        const std::string path = dir + (online ? "online_savegame.dat" : "savegame.dat");
        Save::GameState state{};
        state.generation = 3;
        assert(save(state));
        const std::string original = contents(path);
        assert(!original.empty());
        state.generation = 4;
        for (Fault injected : {Open, Write, Close, FullDisk, Replace}) {
            const int before_sync = syncs, before_close = closes;
            fault = injected;
            const bool ok = save(state);
            fault = None;
            assert(!ok);
            assert(syncs == before_sync);
            assert(closes == before_close + (injected == Open ? 0 : 1));
            assert(contents(path) == original);
            assert(access((path + ".tmp").c_str(), F_OK) != 0);
            Save::GameState restored{};
            assert(load(restored) && restored.generation == 3);
        }
        // An interrupted attempt may leave a partial temporary file.
        { std::ofstream partial(path + ".tmp"); partial << "partial save"; }
        const int before_sync = syncs;
        assert(save(state));
        assert(syncs == before_sync + 1);
        Save::GameState restored{};
        assert(load(restored) && restored.generation == 4);
        assert(access((path + ".tmp").c_str(), F_OK) != 0);
        assert(unlink(path.c_str()) == 0);
    }
    assert(rmdir(dir.c_str()) == 0);
    assert(rmdir((std::string(root) + "/cc.gfm").c_str()) == 0);
    assert(rmdir(root) == 0);
    puts("savegame_test: PASS (solo and online; open/write/close/disk-full/replace failures)");
}
