#ifndef STARTUP_TRACE_H
#define STARTUP_TRACE_H

// NEWTONIA_TRACE: a step-by-step startup/backend trace for launches whose
// stdio reaches nothing. NEWTONIA_TRACE=1 appends to $HOME/newtonia-trace.txt
// (the least typing on a Steam Deck's on-screen keyboard); any other
// non-path value writes to stderr (survives an
// exit that never flushes); NEWTONIA_TRACE=/absolute/path appends to that
// file instead — Steam's runtime container swallowed even unbuffered
// stderr (field, 2026-09-05), so a file of its own is the only channel
// that reliably gets out. Dev-only; no shipped launch sets it. Off, both
// calls cost one cached branch.
//
// Shared by glut.cpp (init steps, SDL's joystick enumeration, main-loop
// entry/return) and steam_input.cpp (Init result, handles, set switches,
// hot-plug) — STEAMINPUT.md §9 asked for the backend's decisions in this
// trace from the first commit.
void startup_trace(const char *step);
// printf-style convenience for the same channel.
void startup_tracef(const char *fmt, ...);
// Whether the channel is open — for per-event lines whose formatting
// should not run when nobody is listening.
bool startup_trace_enabled();

#endif
