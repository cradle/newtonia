// Video capture harness core (NEWTONIA_VIDEO; see video_capture.h and
// shots/README.md). Platform-neutral: the desktop entry point (glut.cpp) owns
// the window and the frame loop and is the only caller; every other platform
// compiles this TU inert.

#include "video_capture.h"

#include "glgame.h"
#include "replay.h"
#include "gl_compat.h"

#include <SDL.h>
#include <SDL_mixer.h>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>
// Frames-on-stdout needs the low-level fd calls; they are spelled differently
// on Windows, which is the platform that needs this path most.
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define FD_NO   _fileno
#define FD_DUP  _dup
#define FD_DUP2 _dup2
#define FD_OPEN _fdopen
#else
#include <unistd.h>
#define FD_NO   fileno
#define FD_DUP  dup
#define FD_DUP2 dup2
#define FD_OPEN fdopen
#endif
#include <vector>

namespace {

std::string s_frame_path;   // NEWTONIA_VIDEO: rgb24 stream (usually a fifo)
std::string s_audio_path;   // NEWTONIA_VIDEO_AUDIO: mixed s16 stream
std::string s_replay_path;  // resolved .nrp

int  s_width = 1920, s_height = 1080;
int  s_fps = 60;
int  s_start_ms = 0;     // skip this far in before the capture begins
int  s_duration_ms = 0;  // 0 = until the recording runs out
bool s_hud = true;       // in-game HUD + minimap
bool s_chrome = false;   // REPLAY watermark / timeline / hints
bool s_info_only = false;
unsigned s_seed = 1337;  // particle/debris rolls; the shot harness's default

FILE   *s_frames = NULL;
bool    s_frames_is_stdout = false;
FILE   *s_audio = NULL;
GLGame *s_game = NULL;

long long s_frames_ticked = 0; // frames of sim time handed to tick()
int  s_ticked_ms = 0;          // where those frames land on the timeline
int  s_captured_ms = 0;        // ticked since the capture began
long s_frame_count = 0;
bool s_started = false;        // past the skip-ahead
bool s_failed = false;
bool s_finished = false;       // finish() has run

// Frame buffers, reused (a 1080p frame is 8 MB — not a per-frame allocation).
std::vector<unsigned char> s_rgba, s_rgb;

// ---- audio pass --------------------------------------------------------
// The postmix hook takes the final mix — every channel plus the music — and
// writes it out. It NEVER waits: SDL holds the audio device lock across the
// callback and the game takes that same lock on every Mix_* call, so a wait
// here stops the game thread dead (see video_capture.h). The pacing is done on
// the main thread instead, in frame_done(), which holds nothing.
//
// s_audio is only touched by the audio thread while s_audio_armed is set;
// finish() clears the hook before closing it.
SDL_atomic_t s_audio_ms;     // ms of mixed audio written
SDL_atomic_t s_audio_armed;  // 1 while the tap is live
long long s_audio_bytes = 0;
int s_audio_bytes_per_sec = 0;
int s_worst_lag_ms = 0;      // furthest the sim ever fell behind the device

// The audio pass runs at the device's rate, so a wait longer than this means
// the device has stopped producing (or never started) — stop, rather than hang
// a headless render nobody is watching.
const Uint32 AUDIO_STALL_MS = 4000;

void set_env(const char *k, const char *v) {
  SDL_setenv(k, v, 1);
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}

void SDLCALL postmix(void *, Uint8 *stream, int len) {
  if (!SDL_AtomicGet(&s_audio_armed) || !s_started || !s_audio) return;
  if (fwrite(stream, 1, (size_t)len, s_audio) != (size_t)len) {
    SDL_AtomicSet(&s_audio_armed, 0);
    SDL_Log("video: audio write failed - the rest of the pass is silent");
    return;
  }
  s_audio_bytes += len;
  SDL_AtomicSet(&s_audio_ms,
                (int)(s_audio_bytes * 1000 / s_audio_bytes_per_sec));
}

void log_header(const std::string &path) {
  Replay::Header h;
  Replay::HeaderStatus st = Replay::read_header_status(path, h);
  if (st != Replay::HEADER_OK) {
    SDL_Log("video: %s has no readable header (%s)", path.c_str(),
            st == Replay::HEADER_TOO_NEW ? "recorded by a newer build"
            : st == Replay::HEADER_TOO_OLD ? "older format"
                                           : "missing or damaged");
    return;
  }
  // duration_ms is the header's, patched at flushes rather than per record, so
  // a crash artifact understates it — the playback timeline itself comes from
  // the records. Treat this the way the replays list does: a guide.
  SDL_Log("video: %s - score %u, level %u, %u:%02u, %u player%s, version %s",
          path.c_str(), h.final_score, h.generation + 1,
          h.duration_ms / 60000, (h.duration_ms / 1000) % 60,
          (unsigned)h.player_count, h.player_count == 1 ? "" : "s",
          h.game_version);
}

}  // namespace

bool VideoCapture::requested() {
  const char *v = SDL_getenv("NEWTONIA_VIDEO");
  if (v && v[0]) return true;
  const char *a = SDL_getenv("NEWTONIA_VIDEO_AUDIO");
  return a != NULL && a[0] != '\0';
}

bool VideoCapture::info_only() { return s_info_only; }
int  VideoCapture::width()  { return s_width; }
int  VideoCapture::height() { return s_height; }
bool VideoCapture::wants_frame() { return s_started && s_frames != NULL; }
bool VideoCapture::ok() {
  // Which pass this was has to come from the PATH, not from the stream handle:
  // ok() is called after finish(), which closed and nulled it.
  if (s_failed) return false;
  return s_frame_path.empty() ? s_audio_bytes > 0 : s_frame_count > 0;
}

bool VideoCapture::init() {
  SDL_AtomicSet(&s_audio_ms, 0);
  SDL_AtomicSet(&s_audio_armed, 0);

  const char *fp = SDL_getenv("NEWTONIA_VIDEO");
  if (fp) s_frame_path = fp;
  const char *ap = SDL_getenv("NEWTONIA_VIDEO_AUDIO");
  if (ap) s_audio_path = ap;
  if (!s_frame_path.empty() && !s_audio_path.empty()) {
    // One pass, one stream. Capturing both at once is the deadlock described
    // in video_capture.h, and silently doing only one would be worse.
    SDL_Log("video: NEWTONIA_VIDEO and NEWTONIA_VIDEO_AUDIO are separate "
            "passes - run the binary twice (shots/video.sh does)");
    return false;
  }

  const char *rp = SDL_getenv("NEWTONIA_VIDEO_REPLAY");
  s_replay_path = Replay::path_for_name(rp && rp[0] ? rp : "best");
  if (s_replay_path.empty()) {
    SDL_Log("video: no replay to render (no pref path?)");
    return false;
  }

  const char *size = SDL_getenv("NEWTONIA_VIDEO_SIZE");
  if (size && size[0]) {
    int w = 0, h = 0;
    if (sscanf(size, "%dx%d", &w, &h) != 2 || w < 64 || h < 64) {
      SDL_Log("video: bad NEWTONIA_VIDEO_SIZE (want WxH): %s", size);
      return false;
    }
    s_width = w;  s_height = h;
  }
  const char *fps = SDL_getenv("NEWTONIA_VIDEO_FPS");
  if (fps && fps[0]) {
    s_fps = atoi(fps);
    if (s_fps < 1 || s_fps > 240) {
      SDL_Log("video: bad NEWTONIA_VIDEO_FPS (1..240): %s", fps);
      return false;
    }
  }
  const char *st = SDL_getenv("NEWTONIA_VIDEO_START_MS");
  if (st && st[0]) s_start_ms = atoi(st);
  if (s_start_ms < 0) s_start_ms = 0;
  const char *dur = SDL_getenv("NEWTONIA_VIDEO_MS");
  if (dur && dur[0]) s_duration_ms = atoi(dur);
  if (s_duration_ms < 0) s_duration_ms = 0;
  const char *hud = SDL_getenv("NEWTONIA_VIDEO_HUD");
  if (hud && hud[0]) s_hud = !(hud[0] == '0' && hud[1] == '\0');
  const char *chrome = SDL_getenv("NEWTONIA_VIDEO_CHROME");
  if (chrome && chrome[0]) s_chrome = !(chrome[0] == '0' && chrome[1] == '\0');
  const char *seed = SDL_getenv("NEWTONIA_VIDEO_SEED");
  if (seed && seed[0]) s_seed = (unsigned)strtoul(seed, NULL, 10);

  log_header(s_replay_path);
  const char *info = SDL_getenv("NEWTONIA_VIDEO_INFO");
  if (info && info[0] == '1' && info[1] == '\0') {
    s_info_only = true;
    return true;
  }

  // Both passes want a working mixer: the audio pass obviously, the video pass
  // so the sim behaves identically in the two (every sound cue takes the same
  // code path and costs the same time). The dummy driver provides that with no
  // device and no noise, and it paces at real time — which is exactly the
  // clock the audio pass throttles itself to. An explicit SDL_AUDIODRIVER is
  // left alone: a real device works just as well, it is only louder.
  if (!SDL_getenv("SDL_AUDIODRIVER")) set_env("SDL_AUDIODRIVER", "dummy");

  // Playback writes no player data of its own; this makes sure a stray
  // recording preference can't have the harness append to a live run either.
  set_env("NEWTONIA_REPLAY_DISABLE", "1");

  if (!s_frame_path.empty()) {
    // Opening the frame stream can BLOCK: a fifo's open waits for a reader.
    // Say what is happening first, so a driver script that never started its
    // encoder looks like a stuck open rather than a stuck game.
    SDL_Log("video: %dx%d @ %d fps -> %s", s_width, s_height, s_fps,
            s_frame_path.c_str());
    if (s_frame_path == "-") {
      // Frames on stdout, for piping straight into an encoder. This is the
      // ONLY mechanism that works on Windows: the POSIX driver hands the game
      // a fifo, and MSYS2's fifos are emulated for MSYS2 programs — a native
      // newtonia.exe cannot open one. A pipe through cmd is byte-clean.
      //
      // Take a DUPLICATE of the pipe for the frames, then point stdout itself
      // at stderr. Every logger in the process then writes somewhere harmless
      // without having to be found and changed one by one — and they do need
      // finding: SDL_Log goes to stdout on this build, as do the "Grid: 6x6"
      // and "Presence:" lines and the controller banner. Redirecting only the
      // C++ stream left 487 bytes of text in the stream, which shifts every
      // frame after it (ffmpeg: "packet size 487 < expected frame_size").
      int fd = FD_DUP(FD_NO(stdout));
      if (fd < 0) {
        SDL_Log("video: cannot duplicate stdout for the frame stream");
        return false;
      }
      s_frames = FD_OPEN(fd, "wb");
      if (!s_frames) {
        SDL_Log("video: cannot open the duplicated stdout");
        return false;
      }
      s_frames_is_stdout = true;
      fflush(stdout);
      FD_DUP2(FD_NO(stderr), FD_NO(stdout));
#ifdef _WIN32
      // Without this the CRT turns every 0x0A in the frame data into 0x0D 0x0A
      // and the video is shredded.
      _setmode(fd, _O_BINARY);
#endif
    } else {
      s_frames = fopen(s_frame_path.c_str(), "wb");
      if (!s_frames) {
        SDL_Log("video: cannot open %s for writing", s_frame_path.c_str());
        return false;
      }
    }
  } else {
    SDL_Log("video: audio pass (%dx%d viewport) -> %s", s_width, s_height,
            s_audio_path.c_str());
    s_audio = fopen(s_audio_path.c_str(), "wb");
    if (!s_audio) {
      SDL_Log("video: cannot open %s for writing", s_audio_path.c_str());
      return false;
    }
  }
  return true;
}

void VideoCapture::audio_start() {
  if (!s_audio) return;
  int freq = 0, channels = 0;
  Uint16 fmt = 0;
  if (!Mix_QuerySpec(&freq, &fmt, &channels) || freq <= 0 || channels <= 0) {
    SDL_Log("video: no audio device - nothing to capture");
    s_failed = true;
    return;
  }
  if (fmt != AUDIO_S16SYS) {
    // Only s16 is worth the plumbing: it is what Mix_OpenAudio asks for
    // everywhere in this game (MIX_DEFAULT_FORMAT), so a different format
    // means something changed, and the raw stream's declared type would be a
    // lie to whatever muxes it.
    SDL_Log("video: audio format 0x%x is not s16 - cannot capture",
            (unsigned)fmt);
    s_failed = true;
    return;
  }
  s_audio_bytes_per_sec = freq * channels * 2;
  SDL_AtomicSet(&s_audio_armed, 1);
  Mix_SetPostMix(postmix, NULL);
  SDL_Log("video: audio %d Hz %d ch s16le (driver %s)", freq, channels,
          SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
}

State *VideoCapture::build_state() {
  // Playback's world comes from the file, but the decoration around it —
  // explosion debris, thruster particles — rolls off rand(), which main()
  // seeded from the clock. Re-seed so a re-render of the same replay is the
  // same video, and so the two passes agree (ShotScene does this too).
  srand(s_seed);
  GLGame *g = GLGame::start_replay_playback(s_replay_path);
  if (!g) {
    s_failed = true;
    return NULL;
  }
  g->shot_hide_hud_ = !s_hud;
  g->replay_hide_chrome_ = !s_chrome;
  s_game = g;
  return g;
}

int VideoCapture::next_delta_ms() {
  // Frame N lands at exactly N*1000/fps ms, and each step is the difference
  // between consecutive landings — so 60 fps is 16,17,17,16,17,17 with NO
  // accumulated rounding. (Stepping by 1000/fps instead makes every frame
  // 16 ms, a 4%-fast video; stepping by a truncated microsecond count fixes
  // that but still drifts a frame every few minutes.)
  s_frames_ticked++;
  int landing = (int)(s_frames_ticked * 1000 / s_fps);
  int ms = landing - s_ticked_ms;
  s_ticked_ms = landing;
  if (ms < 1) ms = 1;
  // The frame that REACHES the start offset is the first captured one, and
  // its own step belongs to the capture — deciding after adding it would
  // charge that step to the skip and leave the render one frame long.
  if (!s_started && s_ticked_ms >= s_start_ms) {
    s_started = true;
    if (s_start_ms > 0)
      SDL_Log("video: capture starts at %d ms into the run", s_ticked_ms);
  }
  if (s_started) s_captured_ms += ms;  // before that, the skip-ahead
  return ms;
}

bool VideoCapture::capture(int window_w, int window_h) {
  if (!s_frames || s_failed) return false;
  // A window manager that clamped the window (or a virtual screen smaller than
  // the requested size) silently changes the frame geometry, and a raw stream
  // carries no geometry — the encoder would keep slicing the declared size out
  // of differently-sized frames and produce a diagonally sheared video. Refuse
  // instead.
  if (window_w != s_width || window_h != s_height) {
    SDL_Log("video: window is %dx%d but %dx%d was requested - the frame "
            "stream has no way to say so; stopping",
            window_w, window_h, s_width, s_height);
    s_failed = true;
    return false;
  }
  size_t pixels = (size_t)window_w * window_h;
  if (s_rgba.size() != pixels * 4) s_rgba.resize(pixels * 4);
  if (s_rgb.size() != pixels * 3) s_rgb.resize(pixels * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, window_w, window_h, GL_RGBA, GL_UNSIGNED_BYTE,
               s_rgba.data());
  // GL's bottom-up rows to the top-down rows every encoder expects, dropping
  // alpha (the same conversion ShotScene::capture does for a PNG).
  for (int y = 0; y < window_h; y++) {
    const unsigned char *src =
        &s_rgba[(size_t)(window_h - 1 - y) * window_w * 4];
    unsigned char *dst = &s_rgb[(size_t)y * window_w * 3];
    for (int x = 0; x < window_w; x++) {
      dst[x * 3 + 0] = src[x * 4 + 0];
      dst[x * 3 + 1] = src[x * 4 + 1];
      dst[x * 3 + 2] = src[x * 4 + 2];
    }
  }
  if (fwrite(s_rgb.data(), 1, s_rgb.size(), s_frames) != s_rgb.size()) {
    SDL_Log("video: frame write failed at frame %ld (encoder gone?)",
            s_frame_count);
    s_failed = true;
    return false;
  }
  s_frame_count++;
  return true;
}

void VideoCapture::frame_done() {
  if (!SDL_AtomicGet(&s_audio_armed) || !s_started) return;
  // The audio pass is the one that has to be paced: the device produces its
  // stream at real time and the sim, with nothing to draw, is faster than
  // that. Hold the sim at the device's shoulder so one second of captured
  // audio covers one second of simulated game. Waiting HERE is safe — the main
  // thread holds no audio lock (see video_capture.h for what happens when the
  // callback tries to do this instead).
  Uint32 t0 = SDL_GetTicks();
  while (SDL_AtomicGet(&s_audio_ms) < s_captured_ms) {
    if (SDL_GetTicks() - t0 > AUDIO_STALL_MS) {
      SDL_Log("video: audio device stalled at %d ms - stopping",
              SDL_AtomicGet(&s_audio_ms));
      s_failed = true;
      return;
    }
    SDL_Delay(1);
  }
  // The other direction is the one this pass cannot hold: the device runs on
  // regardless, so a sim frame that takes longer than real time lets the
  // stream gain audio the sim had no game time for. It does NOT accumulate —
  // the wait above re-pins sim time to the device every frame — so the cost
  // is local: cues around a stall land up to that late. Track the worst and
  // report it; a silently misaligned capture is worth being loud about.
  int lag = SDL_AtomicGet(&s_audio_ms) - s_captured_ms;
  if (lag > s_worst_lag_ms) s_worst_lag_ms = lag;
}

bool VideoCapture::done() {
  if (s_failed) return true;
  if (s_duration_ms > 0 && s_captured_ms >= s_duration_ms) return true;
  // Past the last record the world freezes (REPLAY.md R2) — there is nothing
  // further to show, so the capture ends where the recording does.
  if (s_game && s_game->replay_finished_) return true;
  return false;
}

void VideoCapture::finish() {
  if (s_finished) return;
  s_finished = true;
  if (SDL_AtomicGet(&s_audio_armed)) {
    // Let the device finish the buffer the last frames belong to, so the tail
    // of the capture isn't clipped mid-explosion.
    Uint32 t0 = SDL_GetTicks();
    while (SDL_AtomicGet(&s_audio_ms) < s_captured_ms &&
           SDL_GetTicks() - t0 < 1000)
      SDL_Delay(1);
    SDL_AtomicSet(&s_audio_armed, 0);
  }
  Mix_SetPostMix(NULL, NULL);
  if (s_frames) {
    // stdout belongs to the process, not to us: flush it and let exit close
    // it, or the summary below has nowhere to go on a build that logs there.
    if (s_frames_is_stdout) fflush(s_frames); else fclose(s_frames);
    s_frames = NULL;
  }
  if (s_audio)  { fclose(s_audio);  s_audio = NULL; }

  if (!s_frame_path.empty()) {
    int ms = (int)(s_frame_count * 1000 / s_fps);
    SDL_Log("video: wrote %ld frame%s (%dx%d @ %d fps, %d:%02d)%s",
            s_frame_count, s_frame_count == 1 ? "" : "s", s_width, s_height,
            s_fps, ms / 60000, (ms / 1000) % 60, s_failed ? " - FAILED" : "");
    if (!s_failed && s_frame_count == 0)
      SDL_Log("video: nothing was captured - is NEWTONIA_VIDEO_START_MS past "
              "the end of the recording?");
  } else {
    int ms = s_audio_bytes_per_sec
                 ? (int)(s_audio_bytes * 1000 / s_audio_bytes_per_sec) : 0;
    // The lag figure IS the sync margin, so it is always reported: it is how
    // far the device ever got ahead of the frame the sim was on, and one
    // buffer's worth (~23 ms at the game's 1024-sample chunk) is the floor.
    SDL_Log("video: wrote %lld bytes of audio (%d:%02d, sync margin %d ms)%s",
            (long long)s_audio_bytes, ms / 60000, (ms / 1000) % 60,
            s_worst_lag_ms, s_failed ? " - FAILED" : "");
    // A frame of slack is inaudible; a growing gap means the sim could not
    // keep up with the device and the two passes no longer describe the same
    // instants.
    // One buffer is the floor and inaudible; a big one means the sim was
    // starved. Almost always that is OTHER LOAD, not the game — running this
    // pass beside the video render and x264 on a busy box measured 135 ms
    // where the same window alone measured 23. Do not suggest a smaller
    // --size: this pass never draws, so the frame size costs it nothing.
    if (s_worst_lag_ms > 100)
      SDL_Log("video: WARNING - a sim frame stalled up to %d ms behind the "
              "audio device, so cues around it land up to that late (the "
              "timeline re-pins afterwards, it does not accumulate). Free up "
              "the machine and re-run this pass.",
              s_worst_lag_ms);
  }
}
