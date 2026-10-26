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
// when someone takes a free seat mid-game. A seated joiner nobody has
// described yet is looked up (NetRoomLookup, net_room_lookup.h).

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
  // A lookup answered for this jid with nothing to say about it (a player
  // who never proved who they are): it is known now, as nobody in
  // particular, and asked about with empty identities.
  void note_looked(const std::string &jid);
  // Seated jids nothing has been heard about yet, the lookup's work list.
  std::vector<std::string> unknown_seated() const;

  // The platform's verdict over everyone seated: REFUSE if any is refused,
  // WAIT if any is still undecided, else ALLOW. A jid nothing has been
  // heard about is asked about with empty identities (the default backend
  // allows), unless `hold_unknown`, when it counts as undecided while a
  // lookup is still finding out (NetRoomLookup).
  NetPeerVerdict verdict(bool hold_unknown = false) const;
  // The verdict over everyone seated, as verdict(), asked afresh every
  // time: what was allowed before is asked again, so a description that
  // lands later still counts. Tells the platform about each allowed member
  // with a verified identity once (net_peer_admitted), including one that
  // was let in before its description arrived.
  NetPeerVerdict admit_allowed(bool hold_unknown = false);

private:
  struct Member {
    NetIdentity claimed;   // the joiner's own claim, as the worker relayed it
    NetIdentity attested;  // the worker's verified attestation, once it lands
  };
  NetPeerVerdict verdict_of(const std::string &jid, bool hold_unknown) const;
  std::map<std::string, Member> ids_;
  std::vector<std::string> seated_;
  std::set<std::string> admitted_;  // reported to net_peer_admitted
};

#endif /* NET_ROOM_MEMBERS_H */
