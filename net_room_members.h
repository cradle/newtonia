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
  // The host's latest member list, and which of them took their seat after
  // us (MSG_ROOM_MEMBERS' trailing mask; empty from an older host).
  void set_seated(const std::vector<std::string> &jids,
                  const std::vector<std::string> &newer =
                      std::vector<std::string>()) {
    seated_ = jids;
    newer_.clear();
    newer_.insert(newer.begin(), newer.end());
  }
  const std::vector<std::string> &seated() const { return seated_; }
  void clear_seated() {
    seated_.clear();
    newer_.clear();
  }
  void clear() {
    ids_.clear();
    clear_seated();
    admitted_.clear();
    turned_away_.clear();
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
  //
  // Whoever was here first stays: with `turn_away` given, a refused member
  // who took their seat after us is listed there and counts as undecided
  // while the host turns them away (MSG_MEMBER_REFUSED), instead of
  // refusing the room. One who was here before us still refuses it.
  NetPeerVerdict verdict(bool hold_unknown = false,
                         std::vector<std::string> *turn_away = nullptr) const;
  // The verdict over everyone seated, as verdict(), asked afresh every
  // time: what was allowed before is asked again, so a description that
  // lands later still counts. Tells the platform about each allowed member
  // with a verified identity once (net_peer_admitted), including one that
  // was let in before its description arrived.
  NetPeerVerdict admit_allowed(bool hold_unknown = false,
                               std::vector<std::string> *turn_away = nullptr);
  // True the first time a member is to be turned away: the caller asks the
  // host once per member.
  bool first_turn_away(const std::string &jid) {
    return turned_away_.insert(jid).second;
  }

private:
  struct Member {
    NetIdentity claimed;   // the joiner's own claim, as the worker relayed it
    NetIdentity attested;  // the worker's verified attestation, once it lands
  };
  NetPeerVerdict verdict_of(const std::string &jid, bool hold_unknown,
                            std::vector<std::string> *turn_away) const;
  std::map<std::string, Member> ids_;
  std::vector<std::string> seated_;
  std::set<std::string> newer_;        // seated after us
  std::set<std::string> admitted_;     // reported to net_peer_admitted
  std::set<std::string> turned_away_;  // the host was asked to turn away
};

#endif /* NET_ROOM_MEMBERS_H */
