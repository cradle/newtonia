#include "net_room_lookup.h"

#include <sstream>

#include "net_identity.h"
#include "net_protocol.h"  // NET_LOG
#include "net_signal.h"

// Five tries a couple of seconds apart, each allowed long enough for this
// player's own platform verify (a second or two) plus the room's: about
// half a minute in all before giving up.
static const int LOOKUP_ATTEMPTS = 5;
static const int LOOKUP_ATTEMPT_MS = 6000;
static const int LOOKUP_RETRY_MS = 2000;

static std::string join_ids(const std::vector<std::string> &ids) {
  std::string out;
  for (size_t i = 0; i < ids.size(); i++) {
    if (i) out += ",";
    out += ids[i];
  }
  return out;
}

static std::vector<std::string> split_ids(const std::string &csv) {
  std::vector<std::string> out;
  std::stringstream in(csv);
  std::string id;
  while (std::getline(in, id, ','))
    if (!id.empty()) out.push_back(id);
  return out;
}

void NetRoomLookup::close(const char *why) {
  if (!sig_) return;
  if (why) NET_LOG("net: room lookup ended (%s)\n", why);
  sig_->close();
  delete sig_;
  sig_ = nullptr;
}

void NetRoomLookup::stop() {
  close(nullptr);
  attempts_ = 0;
  attempt_ms_ = 0;
  retry_ms_ = 0;
  failed_ = false;
  asked_.clear();
}

NetRoomLookup::State NetRoomLookup::update(NetRoomMembers &members,
                                           const std::string &room_code,
                                           int delta_ms) {
  if (delta_ms < 1) delta_ms = 1;
  if (members.unknown_seated().empty()) {
    if (sig_ || attempts_) stop();
    return Idle;
  }
  if (failed_) return Failed;
  if (!sig_) {
    if (retry_ms_ > 0) {
      retry_ms_ -= delta_ms;
      return Busy;
    }
    if (attempts_ >= LOOKUP_ATTEMPTS || room_code.empty() ||
        !(sig_ = NetSignal::create())) {
      NET_LOG("net: room lookup gave up after %d attempt(s)\n", attempts_);
      failed_ = true;
      return Failed;
    }
    attempts_++;
    attempt_ms_ = 0;
    asked_ = members.unknown_seated();
    NET_LOG("net: room lookup for %u player(s), attempt %d\n",
            (unsigned)asked_.size(), attempts_);
    sig_->connect_lookup(net_signal_url(), room_code);
    return Busy;
  }
  NetSignal::Event ev;
  while (sig_ && sig_->poll(ev)) {
    switch (ev.kind) {
      case NetSignal::Event::LookReady: {
        // Who is asking decides what the worker may say (an Xbox user id
        // only to a verified Xbox player), so announce as the lobby does.
        const NetIdentity &me = net_local_identity();
        sig_->send_identity(me.platform, me.name,
                            net_local_verify_credential());
        sig_->send_lookup(join_ids(asked_));
        break;
      }
      case NetSignal::Event::Identity:
        if (ev.text2 == "joiner" && !ev.peer.empty())
          members.note_identity(ev.peer, ev.platform, ev.text, ev.verified,
                                ev.key, ev.account);
        break;
      case NetSignal::Event::Looked: {
        if (!ev.verified) {
          // Our own verification hadn't landed (or failed): ask again, by
          // when the credential has usually been minted.
          close("this player is not verified yet");
          retry_ms_ = LOOKUP_RETRY_MS;
          break;
        }
        std::vector<std::string> done = split_ids(ev.text);
        for (size_t i = 0; i < done.size(); i++) members.note_looked(done[i]);
        std::vector<std::string> pending = split_ids(ev.text2);
        NET_LOG("net: room lookup answered (%u settled, %u still joining)\n",
                (unsigned)done.size(), (unsigned)pending.size());
        close(nullptr);
        if (!members.unknown_seated().empty()) retry_ms_ = LOOKUP_RETRY_MS;
        break;
      }
      case NetSignal::Event::Error:
        close(ev.text.empty() ? "error" : ev.text.c_str());
        retry_ms_ = LOOKUP_RETRY_MS;
        break;
      case NetSignal::Event::Closed:
        close("socket closed");
        retry_ms_ = LOOKUP_RETRY_MS;
        break;
      default:
        break;
    }
  }
  if (sig_ && (attempt_ms_ += delta_ms) > LOOKUP_ATTEMPT_MS) {
    close("no answer");
    retry_ms_ = LOOKUP_RETRY_MS;
  }
  if (members.unknown_seated().empty()) {
    stop();
    return Idle;
  }
  return Busy;
}
