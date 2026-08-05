#ifndef VIDEO_CAPTURE_H
#define VIDEO_CAPTURE_H

// Video capture harness (NEWTONIA_VIDEO; see shots/README.md).
//
// The screenshot harness's cousin: instead of one composed scene captured at
// one instant, this renders a RECORDED REPLAY (REPLAY.md) frame by frame on a
// fixed timestep, writing raw rgb24 frames — a fifo the driver script feeds to
// ffmpeg — or the audio that run produced. What comes out is a gameplay video
// of a real run, reproducible from the command line, which is what a store
// page keeps needing new versions of.
//
//   NEWTONIA_VIDEO=frames.raw          video pass; rgb24 frame stream (a fifo)
//   NEWTONIA_VIDEO_AUDIO=audio.raw     audio pass; mixed s16 stream
//   NEWTONIA_VIDEO_REPLAY=best         .nrp path, or current/recent/best/
//                                      bestcoop/online/last (as NEWTONIA_REPLAY_PLAY)
//   NEWTONIA_VIDEO_SIZE=1920x1080      frame size (default 1920x1080)
//   NEWTONIA_VIDEO_FPS=60              frame rate (default 60)
//   NEWTONIA_VIDEO_START_MS=0          skip this far into the run first
//   NEWTONIA_VIDEO_MS=0                capture this much (0 = to the end)
//   NEWTONIA_VIDEO_HUD=0               drop the in-game HUD and minimap
//   NEWTONIA_VIDEO_CHROME=1            keep the REPLAY watermark/timeline
//   NEWTONIA_VIDEO_INFO=1              log the replay's header and exit
//
// The video is NOT a screen recording: frame N is always exactly one frame
// time after frame N-1, so a slow headless GL context yields a smooth 60 fps
// video instead of a stuttery one, and the same file always renders the same
// frames.
//
// ---- why picture and sound are two separate passes ----
//
// The mixer fills buffers from its own thread on the audio device's clock,
// which is not the clock an offline render runs on — so captured audio would
// drift against the video from the first second. The obvious fix, pacing the
// two against each other by making the postmix hook wait for the render, does
// not work and cannot be made to work: SDL calls the audio callback with the
// device lock HELD, and every Mix_* call the game makes (the per-tick volume
// updates alone are dozens) takes that same lock. Waiting inside the callback
// therefore blocks the game thread inside tick(), which is what the callback
// is waiting for — a deadlock that resolves only when the wait times out.
// Measured, before this was understood: 53 seconds to render 3 seconds of
// video, 52 of them inside tick().
//
// So neither pass tries. The VIDEO pass renders as fast as the machine can and
// captures no audio. The AUDIO pass replays the same file with the same fixed
// timestep and the same window geometry (the distance attenuation reads the
// viewport — CLAUDE.md "Audio") but never draws, and throttles ITSELF to the
// audio device's real-time rate, waiting on the main thread where no lock is
// held. Same file, same steps, same sound cues at the same sim times: the two
// streams line up by construction, and the driver script muxes them. The audio
// pass costs the replay's own duration in wall clock, and warns if the sim
// could not keep up with it.
//
// Like shot mode: no Steam, no achievements, no preference writes, no saves.
// Playback already writes nothing (REPLAY.md R2) and this adds nothing beyond
// the stream it was pointed at.
//
// This class is the platform-neutral core; the desktop entry point (glut.cpp)
// owns the window and the frame loop and is the only caller. Other platforms
// compile this TU but never call it.

class State;

class VideoCapture {
public:
  // True when NEWTONIA_VIDEO or NEWTONIA_VIDEO_AUDIO is set — the desktop
  // entry point then runs the capture loop instead of the interactive game.
  static bool requested();
  // Parse the env and open the output stream. Call BEFORE SDL_Init: it picks
  // the audio driver the pass needs. false = decline (already logged).
  static bool init();
  // NEWTONIA_VIDEO_INFO: the header was logged, there is nothing to render.
  static bool info_only();
  static int width();
  static int height();
  // Install the audio tap; call after Mix_OpenAudio. A no-op in the video
  // pass, or if no device opened (the pass then produces silence and says so).
  static void audio_start();
  // The replay playback game, or NULL (missing/unreadable/older format).
  // Needs a live GL context — the constructors upload meshes.
  static State *build_state();
  // Milliseconds for the next tick(). Fixed by the frame rate, accumulated in
  // microseconds so 60 fps is 16,17,17,16,... not a 4%-fast flat 16.
  static int next_delta_ms();
  // True when this tick's frame should be drawn and captured: the video pass,
  // past the skip-ahead. The audio pass never draws.
  static bool wants_frame();
  // glReadPixels the back buffer (BEFORE the swap, like ShotScene::capture)
  // and write one frame. false = the stream broke; the run stops.
  static bool capture(int window_w, int window_h);
  // End of a simulated frame: advance the published clock and, in the audio
  // pass, hold the sim back to the audio device's rate.
  static void frame_done();
  // The recording ran out, the requested duration elapsed, or something broke.
  static bool done();
  // Close the stream and log the summary. Safe to call twice.
  static void finish();
  // Exit status for the process.
  static bool ok();
};

#endif
