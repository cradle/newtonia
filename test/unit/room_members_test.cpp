// MSG_ROOM_MEMBERS body (net_protocol.h): the host's list of the other
// relay joiners seated with a client. Round trip, the writer's caps, and a
// reader that refuses anything it could not have written.
#include <cstdio>
#include <string>
#include <vector>

#include "net_protocol.h"

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

  if (failures) return 1;
  std::printf("ROOM-MEMBERS-TEST-OK\n");
  return 0;
}
