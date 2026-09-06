// See startup_trace.h.

#include "startup_trace.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

static FILE *startup_trace_out() {
  static FILE *out = NULL;
  static bool decided = false;
  if (!decided) {
    decided = true;
    // std::getenv, not SDL_getenv: this TU stays SDL-free so the unit
    // tests can link it without an SDL runtime.
    const char *t = std::getenv("NEWTONIA_TRACE");
    if (t && t[0] == '/') out = fopen(t, "a");
    else if (t) out = stderr;
  }
  return out;
}

void startup_trace(const char *step) {
  FILE *out = startup_trace_out();
  if (out) { fprintf(out, "trace: %s\n", step); fflush(out); }
}

void startup_tracef(const char *fmt, ...) {
  FILE *out = startup_trace_out();
  if (!out) return;
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  fprintf(out, "trace: %s\n", line);
  fflush(out);
}
