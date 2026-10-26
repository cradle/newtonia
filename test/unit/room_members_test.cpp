// MSG_ROOM_MEMBERS body (net_protocol.h): the host's list of the other
// relay joiners seated with a client. Round trip, the writer's caps, and a
// reader that refuses anything it could not have written. Then
// NetRoomMembers (net_room_members.h), the verdict over who is seated,
// against a stand-in platform policy: a claimed-only Xbox player is
// undecided, a player named BAD is refused, everyone else allowed.
#include <cstdio>
#include <string>
#include <vector>

#include "net_protocol.h"
#include "net_room_members.h"

static int admitted_calls = 0;
NetPeerVerdict net_peer_verdict(const NetIdentity &claimed,
                                const NetIdentity &attested) {
  if (attested.name == "BAD") return NET_PEER_REFUSE;
  if (claimed.platform == NET_PLATFORM_XBOX &&
      attested.platform_trust != NET_TRUST_ATTESTED)
    return NET_PEER_WAIT;
  return NET_PEER_ALLOW;
}
void net_peer_admitted(const NetIdentity &) { admitted_calls++; }
std::string net_sanitize_name(const std::string &raw) { return raw; }
const char *net_platform_label(uint8_t) { return "X"; }

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) { std::printf("FAIL: %s\n", what); failures++; }
}

static std::vector<std::string> round_trip(const std::vector<std::string> &in,
                                           bool *ok) {
  std::vector<uint8_t> buf;
  Net::put_room_members(buf, in);
  Net::Reader r(buf);
  std::vector<std::string> out;
  *ok = Net::read_room_members(r, out);
  return out;
}

int main() {
  bool ok = false;
  std::vector<std::string> two;
  two.push_back("4");
  two.push_back("17");
  check(round_trip(two, &ok) == two && ok, "two ids round trip");

  std::vector<std::string> none;
  check(round_trip(none, &ok).empty() && ok, "an empty list round trips");

  // The writer drops what a reader would refuse: empty or overlong ids,
  // and anything past the count cap.
  std::vector<std::string> messy;
  messy.push_back("");
  messy.push_back(std::string(Net::ROOM_MEMBER_ID_MAX + 1, '9'));
  messy.push_back("1");
  messy.push_back("2");
  messy.push_back("3");
  messy.push_back("4");
  std::vector<std::string> got = round_trip(messy, &ok);
  check(ok && got.size() == Net::ROOM_MEMBERS_MAX && got[0] == "1" &&
            got[2] == "3",
        "writer keeps valid ids up to the cap");

  // Malformed bodies.
  const uint8_t too_many[] = {4, 1, '1', 1, '2', 1, '3', 1, '4'};
  Net::Reader r1(too_many, sizeof(too_many));
  check(!Net::read_room_members(r1, got), "count over the cap refused");
  const uint8_t short_id[] = {1, 5, '1', '2'};
  Net::Reader r2(short_id, sizeof(short_id));
  check(!Net::read_room_members(r2, got), "truncated id refused");
  const uint8_t zero_len[] = {1, 0};
  Net::Reader r3(zero_len, sizeof(zero_len));
  check(!Net::read_room_members(r3, got), "empty id refused");
  Net::Reader r4(nullptr, 0);
  check(!Net::read_room_members(r4, got), "empty body refused");

  // NetRoomMembers.
  NetRoomMembers m;
  check(m.verdict() == NET_PEER_ALLOW, "nobody seated: allowed");
  std::vector<std::string> seated;
  seated.push_back("2");
  seated.push_back("3");
  m.set_seated(seated);
  check(m.verdict() == NET_PEER_ALLOW,
        "undescribed members are asked about as-is (allowed here)");
  check(m.verdict(true) == NET_PEER_WAIT,
        "undescribed members wait while frames are expected");
  m.note_identity("2", NET_PLATFORM_XBOX, "", false, "", "");
  check(m.verdict() == NET_PEER_WAIT, "a claimed Xbox member waits");
  m.note_identity("2", NET_PLATFORM_XBOX, "", true, "k", "XUID-2");
  m.note_identity("3", NET_PLATFORM_STEAM, "PC", true, "", "");
  check(m.verdict() == NET_PEER_ALLOW, "attested members allowed");
  check(m.admit_allowed() == NET_PEER_ALLOW && admitted_calls == 2,
        "both members reported admitted");
  check(m.admit_allowed() == NET_PEER_ALLOW && admitted_calls == 2,
        "each member reported once");
  // A newcomer mid-game: undescribed at first, then refused.
  seated.push_back("5");
  m.set_seated(seated);
  check(m.admit_allowed(true) == NET_PEER_WAIT && admitted_calls == 2,
        "an undescribed newcomer waits while frames are expected");
  m.note_identity("5", NET_PLATFORM_XBOX, "BAD", true, "", "XUID-5");
  check(m.admit_allowed(true) == NET_PEER_REFUSE, "a refused newcomer");
  check(m.verdict() == NET_PEER_REFUSE, "the room verdict carries it");
  m.clear();
  check(m.seated().empty() && !m.known("2"), "clear forgets the room");

  // Let in before its description arrived (review on #628): the member is
  // asked about again once described, refused, and the late description of
  // an allowed one is reported then.
  NetRoomMembers late;
  std::vector<std::string> one;
  one.push_back("2");
  late.set_seated(one);
  int before = admitted_calls;
  check(late.admit_allowed() == NET_PEER_ALLOW && admitted_calls == before,
        "an undescribed member let in has nothing to report");
  late.note_identity("2", NET_PLATFORM_STEAM, "BAD", true, "", "");
  check(late.admit_allowed(true) == NET_PEER_REFUSE,
        "a member described after it was let in is asked about again");
  NetRoomMembers late2;
  late2.set_seated(one);
  late2.admit_allowed();
  late2.note_identity("2", NET_PLATFORM_STEAM, "PC", true, "", "");
  check(late2.admit_allowed() == NET_PEER_ALLOW && admitted_calls == before + 1,
        "a late description is reported once it lands");

  // The lookup's work list and its "nothing on record" answer.
  NetRoomMembers lk;
  std::vector<std::string> three;
  three.push_back("2");
  three.push_back("3");
  three.push_back("4");
  lk.set_seated(three);
  lk.note_identity("3", NET_PLATFORM_STEAM, "PC", true, "", "");
  check(lk.unknown_seated().size() == 2 && lk.unknown_seated()[0] == "2",
        "undescribed seated members are the lookup's work list");
  lk.note_looked("2");
  lk.note_looked("4");
  check(lk.unknown_seated().empty() && lk.known("4"),
        "a looked-up member is known even with nothing on record");
  check(lk.verdict(true) == NET_PEER_ALLOW,
        "nothing on record is asked about as-is, no longer held");

  if (failures) return 1;
  std::printf("ROOM-MEMBERS-TEST-OK\n");
  return 0;
}
