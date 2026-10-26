#ifndef NET_ROOM_MEMBERS_H
#define NET_ROOM_MEMBERS_H

// The other joiners a 3-4 player room seats with this client, for the
// platform's per-peer verdict (net_policy.h): an Xbox console checks its
// player's block list and privacy settings against everyone it plays with,
// not only the host. Two sources, deliberately split: the signalling worker
// says who each joiner is (identity frames stamped with their jid, sent
// only to Xbox-verified joiners; signal/src/worker.js identity_frame), and
// the HOST says which of them are seated with us (MSG_ROOM_MEMBERS), since
// the worker can't tell a seated joiner from one the host turned away.
// Used by the lobby before the game starts and by the game afterwards,
// when someone takes a free seat mid-game.

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "net_identity.h"
#include "net_policy.h"

class NetRoomMembers {
public:
  // A worker identity frame about another joiner. Unverified frames are the
  // joiner's own claim, which the platform may wait on (a claimed Xbox
  // player it can't check yet); a verified one is the attestation.
  void note_identity(const std::string &jid, uint8_t platform,
                     const std::string &name, bool verified,
                     const std::string &key, const std::string &account);
  // The host's latest member list.
  void set_seated(const std::vector<std::string> &jids) { seated_ = jids; }
  const std::vector<std::string> &seated() const { return seated_; }
  void clear_seated() { seated_.clear(); }
  void clear() {
    ids_.clear();
    seated_.clear();
    admitted_.clear();
  }
  bool known(const std::string &jid) const { return ids_.count(jid) != 0; }

  // The platform's verdict over everyone seated: REFUSE if any is refused,
  // WAIT if any is still undecided, else ALLOW. A jid the worker never
  // described is asked about with empty identities (the default backend
  // allows), unless `hold_unknown`, when it counts as undecided: a watching
  // game expects its frame shortly (GLGame::net_client_watch_update).
  NetPeerVerdict verdict(bool hold_unknown = false) const;
  // Tells the platform about every seated member it now allows and hasn't
  // been told about yet (net_peer_admitted, once per jid), and returns the
  // verdict over the ones still not admitted (ALLOW when none are left).
  NetPeerVerdict admit_allowed(bool hold_unknown = false);

private:
  struct Member {
    NetIdentity claimed;   // the joiner's own claim, as the worker relayed it
    NetIdentity attested;  // the worker's verified attestation, once it lands
  };
  NetPeerVerdict verdict_of(const std::string &jid, bool hold_unknown) const;
  std::map<std::string, Member> ids_;
  std::vector<std::string> seated_;
  std::set<std::string> admitted_;
};

#endif /* NET_ROOM_MEMBERS_H */
