#include "atomic_file.h"

#include <SDL.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>   // _getpid
#define atomic_file_pid _getpid
#else
#include <unistd.h>    // getpid
#define atomic_file_pid getpid
#endif

namespace AtomicFile {

std::string temp_sibling(const std::string &path) {
  char suffix[32];
  snprintf(suffix, sizeof(suffix), ".%ld.tmp", (long)atomic_file_pid());
  return path + suffix;
}

bool replace(const std::string &from, const std::string &to) {
#ifdef _WIN32
  // Paths from SDL are UTF-8; the wide API is the one that replaces.
  wchar_t *w_from = reinterpret_cast<wchar_t *>(SDL_iconv_string(
      "UTF-16LE", "UTF-8", from.c_str(), from.size() + 1));
  wchar_t *w_to = reinterpret_cast<wchar_t *>(SDL_iconv_string(
      "UTF-16LE", "UTF-8", to.c_str(), to.size() + 1));
  bool ok = w_from && w_to && MoveFileExW(w_from, w_to, MOVEFILE_REPLACE_EXISTING);
  SDL_free(w_from);
  SDL_free(w_to);
  return ok;
#else
  return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool write(const std::string &path,
           const std::function<bool(FILE *)> &write, const char *what) {
  const std::string temporary = temp_sibling(path);
  FILE *fp = fopen(temporary.c_str(), "wb");
  if (!fp) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "%s: cannot create %s: %s (previous file kept)", what,
                 temporary.c_str(), std::strerror(errno));
    return false;
  }
  bool ok = write(fp);
  // Buffered writes can succeed and only report a full disk on close.
  // Always close, even when the body has already failed.
  if (fclose(fp) != 0) ok = false;
  if (ok) ok = replace(temporary, path);
  if (!ok) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "%s: write or replacement failed for %s (previous file kept)",
                 what, path.c_str());
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

}  // namespace AtomicFile
