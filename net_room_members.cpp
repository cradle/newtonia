#include "net_room_members.h"

#include "net_protocol.h"  // NET_LOG

void NetRoomMembers::note_identity(const std::string &jid, uint8_t platform,
                                   const std::string &name, bool verified,
                                   const std::string &key,
                                   const std::string &account) {
  Member &m = ids_[jid];
  NetIdentity id;
  id.platform = platform;
  id.name = net_sanitize_name(name);
  if (verified) {
    id.platform_trust = NET_TRUST_ATTESTED;
    id.name_trust = id.name.empty() ? NET_TRUST_ABSENT : NET_TRUST_ATTESTED;
    id.ban_token = key;
    id.platform_account = account;
    m.attested = id;
  } else {
    id.platform_trust = NET_TRUST_CLAIMED;
    id.name_trust = id.name.empty() ? NET_TRUST_ABSENT : NET_TRUST_CLAIMED;
  }
  // The claim follows the latest frame either way: a verified frame says
  // what platform the joiner is on as surely as its claim did.
  if (!verified || m.claimed.platform == 0) {
    m.claimed.platform = platform;
    m.claimed.platform_trust = NET_TRUST_CLAIMED;
  }
  NET_LOG("net: room member %s identity %s platform=%s(%u)\n", jid.c_str(),
          verified ? "attested" : "claimed", net_platform_label(platform),
          (unsigned)platform);
}

void NetRoomMembers::note_looked(const std::string &jid) {
  if (ids_.count(jid)) return;
  ids_[jid];
  NET_LOG("net: room member %s has no identity on record\n", jid.c_str());
}

std::vector<std::string> NetRoomMembers::unknown_seated() const {
  std::vector<std::string> out;
  for (size_t i = 0; i < seated_.size(); i++)
    if (!ids_.count(seated_[i])) out.push_back(seated_[i]);
  return out;
}

NetPeerVerdict NetRoomMembers::verdict_of(const std::string &jid,
                                          bool hold_unknown) const {
  std::map<std::string, Member>::const_iterator it = ids_.find(jid);
  if (it == ids_.end()) {
    if (hold_unknown) return NET_PEER_WAIT;
    static const Member kUnknown;
    return net_peer_verdict(kUnknown.claimed, kUnknown.attested);
  }
  return net_peer_verdict(it->second.claimed, it->second.attested);
}

NetPeerVerdict NetRoomMembers::verdict(bool hold_unknown) const {
  NetPeerVerdict out = NET_PEER_ALLOW;
  for (size_t i = 0; i < seated_.size(); i++) {
    NetPeerVerdict v = verdict_of(seated_[i], hold_unknown);
    if (v == NET_PEER_REFUSE) return NET_PEER_REFUSE;
    if (v == NET_PEER_WAIT) out = NET_PEER_WAIT;
  }
  return out;
}

NetPeerVerdict NetRoomMembers::admit_allowed(bool hold_unknown) {
  NetPeerVerdict out = NET_PEER_ALLOW;
  for (size_t i = 0; i < seated_.size(); i++) {
    const std::string &jid = seated_[i];
    NetPeerVerdict v = verdict_of(jid, hold_unknown);
    if (v == NET_PEER_REFUSE) return NET_PEER_REFUSE;
    if (v == NET_PEER_WAIT) {
      out = NET_PEER_WAIT;
      continue;
    }
    // Only a verified identity has anything to report; a member described
    // later is reported then.
    if (admitted_.count(jid)) continue;
    std::map<std::string, Member>::const_iterator it = ids_.find(jid);
    if (it == ids_.end() ||
        it->second.attested.platform_trust != NET_TRUST_ATTESTED)
      continue;
    net_peer_admitted(it->second.attested);
    admitted_.insert(jid);
  }
  return out;
}
