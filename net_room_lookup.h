#ifndef NET_ROOM_LOOKUP_H
#define NET_ROOM_LOOKUP_H

// Finds out who a 3-4 player room's other joiners are when nothing has
// described them yet (NetRoomMembers::unknown_seated): someone the host
// seated mid-game, or a joiner whose relay socket closed before this
// player's own verification landed. It opens a short-lived lookup socket to
// the room's worker (NetSignal::connect_lookup), announces this player so
// the worker knows what it may say, asks by the ids the host named, and
// closes again; the worker answers from its records, so the answer never
// depends on having been connected at the right moment. A failed or
// partial answer is asked again a few times, then the lookup gives up and
// the caller leaves the room (an Xbox console may not play on with someone
// it could not check). Used only where the platform checks other players
// (an Xbox player in a worker room).

#include <string>
#include <vector>

#include "net_room_members.h"

class NetSignal;

class NetRoomLookup {
public:
  enum State {
    Idle,    // every seated member is known
    Busy,    // still finding out (hold the unknown ones)
    Failed,  // gave up: the caller leaves the room
  };
  ~NetRoomLookup() { stop(); }
  // Call every tick. Opens, polls and retries the lookup socket for
  // whoever in `members` is seated but unknown, folding the answers in.
  State update(NetRoomMembers &members, const std::string &room_code,
               int delta_ms);
  // Closes any socket and forgets the attempts (the room is left or done).
  void stop();

private:
  void close(const char *why);
  NetSignal *sig_ = nullptr;
  std::vector<std::string> asked_;
  int attempts_ = 0;
  int attempt_ms_ = 0;
  int retry_ms_ = 0;
  bool failed_ = false;
};

#endif /* NET_ROOM_LOOKUP_H */
