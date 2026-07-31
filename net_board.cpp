// Shared half of the leaderboard seam (see net_board.h): frame builders,
// frame parsing (including the top-rows array), URL resolution and the
// availability gate. The WebSocket itself lives in the per-platform
// backend (net_board_rtc.cpp).

#include "net_board.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_policy.h"
#include "net_signal.h"  // NetSig json helpers (shared with signaling)

// The baked default board worker. Overridable at COMPILE time, mirroring
// net_signal.cpp's scheme; the runtime NEWTONIA_BOARD_URL env var still
// wins over whatever is baked here.
#ifndef NEWTONIA_BOARD_URL_DEFAULT
#  if defined(NEWTONIA_BOARD_BETA)
#    define NEWTONIA_BOARD_URL_DEFAULT "wss://newtonia-board-beta.gfmcc.workers.dev/board"
#  else
#    define NEWTONIA_BOARD_URL_DEFAULT "wss://newtonia-board.gfmcc.workers.dev/board"
#  endif
#endif
static const char *BOARD_URL_DEFAULT = NEWTONIA_BOARD_URL_DEFAULT;

std::string net_board_url() {
  const char *env = getenv("NEWTONIA_BOARD_URL");
  if (env && env[0]) return env;
  return BOARD_URL_DEFAULT;
}

bool net_board_available() {
  if (!net_online_play_allowed()) return false;
  NetBoard *probe = NetBoard::create();
  if (!probe) return false;
  delete probe;
  return true;
}

namespace NetBoardProto {

using NetSig::json_escape;
using NetSig::json_field;
using NetSig::json_uint_field;
using NetSig::json_bool_field;

std::string qualify_frame(const std::string &season, int players,
                          uint32_t score) {
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"t\":\"qualify\",\"season\":\"%s\",\"players\":%d,\"score\":%u}",
           json_escape(season).c_str(), players, score);
  return buf;
}

std::string top_frame(const std::string &season, int players, int count) {
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"t\":\"top\",\"season\":\"%s\",\"players\":%d,\"count\":%d}",
           json_escape(season).c_str(), players, count);
  return buf;
}

std::string rank_of_frame(const std::string &season, int players,
                          uint32_t score) {
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"t\":\"rank-of\",\"season\":\"%s\",\"players\":%d,\"score\":%u}",
           json_escape(season).c_str(), players, score);
  return buf;
}

std::string submit_frame(size_t size, uint8_t platform,
                         const std::string &name, const std::string &cred) {
  std::string out = "{\"t\":\"submit\",\"size\":";
  char num[48];
  snprintf(num, sizeof(num), "%lu", (unsigned long)size);
  out += num;
  snprintf(num, sizeof(num), ",\"platform\":%u,\"name\":\"", (unsigned)platform);
  out += num;
  out += json_escape(name);
  out += "\",\"cred\":\"";
  out += json_escape(cred);
  out += "\"}";
  return out;
}

std::string submit_end_frame() { return "{\"t\":\"submit-end\"}"; }

std::string fetch_frame(const std::string &season,
                        const std::string &run_id) {
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"t\":\"fetch\",\"season\":\"%s\",\"run_id\":\"%s\"}",
           json_escape(season).c_str(), json_escape(run_id).c_str());
  return buf;
}

bool is_submit_ok(const std::string &frame) {
  std::string t;
  return json_field(frame, "t", t) && t == "submit-ok";
}

bool fetch_ok_size(const std::string &frame, size_t *size_out) {
  std::string t;
  if (!json_field(frame, "t", t) || t != "fetch-ok") return false;
  unsigned size = 0;
  if (!json_uint_field(frame, "size", size)) return false;
  *size_out = size;
  return true;
}

bool is_fetch_end(const std::string &frame) {
  std::string t;
  return json_field(frame, "t", t) && t == "fetch-end";
}

// Extract the "rows":[{...},{...}] array elements as individual object
// strings, respecting string quoting and escapes (an attested display name
// can legally contain braces). Flat objects only — the protocol nests no
// deeper.
static bool split_rows(const std::string &frame,
                       std::vector<std::string> &out) {
  size_t at = frame.find("\"rows\"");
  if (at == std::string::npos) return false;
  at = frame.find('[', at);
  if (at == std::string::npos) return false;
  size_t i = at + 1;
  while (i < frame.size() && frame[i] != ']') {
    if (frame[i] == '{') {
      size_t start = i;
      bool in_str = false;
      for (; i < frame.size(); i++) {
        char c = frame[i];
        if (in_str) {
          if (c == '\\') { i++; continue; }
          if (c == '"') in_str = false;
        } else if (c == '"') {
          in_str = true;
        } else if (c == '}') {
          out.push_back(frame.substr(start, i - start + 1));
          i++;
          break;
        }
      }
      if (in_str) return false;  // unterminated string: bad frame
    } else {
      i++;
    }
  }
  return i < frame.size();
}

static bool parse_row(const std::string &obj, NetBoard::Row &row) {
  unsigned v = 0;
  if (json_uint_field(obj, "rank", v)) row.rank = (int)v;
  json_field(obj, "name", row.name);
  if (json_uint_field(obj, "platform", v)) row.platform = (uint8_t)v;
  json_bool_field(obj, "verified", row.verified);
  if (json_uint_field(obj, "score", v)) row.score = v;
  if (json_uint_field(obj, "generation", v)) row.generation = v;
  if (json_uint_field(obj, "duration_ms", v)) row.duration_ms = v;
  // `date` is epoch MS — it overflows json_uint_field's unsigned on
  // 32-bit longs, so parse it by hand.
  size_t at = obj.find("\"date\":");
  if (at != std::string::npos) {
    row.date = strtoull(obj.c_str() + at + 7, NULL, 10);
  }
  json_bool_field(obj, "has_replay", row.has_replay);
  json_field(obj, "run_id", row.run_id);
  return row.rank > 0 && !row.run_id.empty();
}

bool parse_frame(const std::string &frame, NetBoard::Event &ev) {
  std::string t;
  if (!json_field(frame, "t", t)) return false;
  if (t == "qualify") {
    ev.kind = NetBoard::Event::Qualify;
    unsigned v = 0;
    ev.place = json_uint_field(frame, "place", v) ? (int)v : 0;
    // cutline is null while the board isn't full — the uint parse fails
    // there and -1 carries "no cut-line yet".
    ev.cutline = json_uint_field(frame, "cutline", v) ? (long)v : -1;
    ev.would_place = false;
    json_bool_field(frame, "would_place", ev.would_place);
    return true;
  }
  if (t == "placed") {
    ev.kind = NetBoard::Event::Placed;
    unsigned v = 0;
    ev.place = json_uint_field(frame, "rank", v) ? (int)v : 0;
    return true;
  }
  if (t == "rank-of") {
    ev.kind = NetBoard::Event::RankOf;
    unsigned v = 0;
    ev.place = json_uint_field(frame, "place", v) ? (int)v : 0;
    return true;
  }
  if (t == "top") {
    ev.kind = NetBoard::Event::Top;
    ev.rows.clear();
    std::vector<std::string> objs;
    if (!split_rows(frame, objs)) return false;
    for (size_t i = 0; i < objs.size(); i++) {
      NetBoard::Row row;
      if (parse_row(objs[i], row)) ev.rows.push_back(row);
    }
    return true;
  }
  if (t == "err") {
    ev.kind = NetBoard::Event::Error;
    json_field(frame, "reason", ev.reason);
    if (ev.reason.empty()) ev.reason = "protocol";
    return true;
  }
  return false;  // submit-ok / fetch-ok / fetch-end: backend-internal
}

}  // namespace NetBoardProto

// ---- factory ------------------------------------------------------------

#if defined(NEWTONIA_NET_DISABLED)
NetBoard *NetBoard::create() { return nullptr; }
#elif defined(NEWTONIA_NET_RTC)
NetBoard *net_board_create_rtc();
NetBoard *NetBoard::create() { return net_board_create_rtc(); }
#else
// Web (v1: no leaderboard on web — LEADERBOARD.md) and netless builds.
NetBoard *NetBoard::create() { return nullptr; }
#endif
