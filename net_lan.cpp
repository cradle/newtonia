// LAN discovery + pairing (see net_lan.h). The real implementation runs
// wherever raw UDP broadcast works — desktop native AND Android (round
// 3; NewtoniaActivity holds a MulticastLock so the wifi driver delivers
// broadcast). One wire protocol everywhere is the point: a phone must
// discover a DESKTOP host, so mDNS/Bonjour backends would need a second
// advertise+browse stack on every platform. iOS has the same code but
// stays opt-in behind NEWTONIA_LAN_IOS until the Apple-gated multicast
// entitlement (com.apple.developer.networking.multicast) is granted —
// without it iOS silently drops broadcast both ways, and the lobby's
// "VISIBLE ON THIS NETWORK" line would lie. Web/Xbox compile the no-op
// stubs (available() false) so the lobby needs no ifdefs — the same
// pattern as invites/presence.

#include "net_lan.h"

#include "net_protocol.h"  // PROTO_VERSION, NET_LOG

#if defined(NEWTONIA_NET_RTC) && !defined(__EMSCRIPTEN__) && \
    !defined(_GAMING_XBOX)
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE || defined(NEWTONIA_LAN_IOS)
#define NEWTONIA_LAN 1
#endif
#else
#define NEWTONIA_LAN 1
#endif
#endif

#ifdef NEWTONIA_LAN

#include <cctype>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>  // INTERFACE_INFO / SIO_GET_INTERFACE_LIST
typedef SOCKET lan_socket_t;
#define LAN_INVALID INVALID_SOCKET
#define lan_close closesocket
static bool lan_would_block() {
  int e = WSAGetLastError();
  return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
static void lan_set_nonblocking(lan_socket_t s) {
  u_long on = 1;
  ioctlsocket(s, FIONBIO, &on);
}
static bool lan_sockets_init() {
  static bool done = false, ok = false;
  if (!done) {
    WSADATA wsa;
    ok = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    done = true;
  }
  return ok;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>  // IFF_UP / IFF_BROADCAST / IFF_LOOPBACK
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/ioctl.h>  // SIOCGIFCONF interface walk (getifaddrs is API 24+)
#else
#include <ifaddrs.h>
#endif
typedef int lan_socket_t;
#define LAN_INVALID (-1)
#define lan_close ::close
static bool lan_would_block() {
  return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
}
static void lan_set_nonblocking(lan_socket_t s) {
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
}
static bool lan_sockets_init() { return true; }
#endif

namespace NetLan {

namespace {

const char MAGIC[4] = {'N', 'W', 'L', 'N'};
const uint8_t BEACON_VERSION = 1;
const int BEACON_INTERVAL_MS = 1000;
// Rows fade after ~2 missed beacons (no flicker on one lost packet).
const int HOST_EXPIRE_MS = 2500;
const int EXCHANGE_TIMEOUT_MS = 10000;
const size_t MAX_BLOB = 64 * 1024;  // framing sanity cap
const size_t MAX_NAME = 24;
const int MAX_HOSTS = 4;

uint16_t lan_port() {
  // Overridable for tests (two instances on one CI box must not collide
  // with a developer's real session).
  const char *e = std::getenv("NEWTONIA_LAN_PORT");
  if (e) {
    int p = std::atoi(e);
    if (p > 0 && p < 65536) return (uint16_t)p;
  }
  return 42607;
}

// Length-framed non-blocking sends/reads: progress lives in the caller's
// buffer+offset, so update() can resume mid-frame every tick.
bool send_some(lan_socket_t s, const std::string &buf, size_t &off,
               bool &error) {
  while (off < buf.size()) {
    int n = (int)send(s, buf.data() + off, (int)(buf.size() - off), 0);
    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n < 0 && lan_would_block()) return false;
    error = true;
    return false;
  }
  return true;
}

// Appends whatever is readable to buf; false = peer closed or errored.
bool recv_some(lan_socket_t s, std::string &buf) {
  char tmp[4096];
  for (;;) {
    int n = (int)recv(s, tmp, (int)sizeof(tmp), 0);
    if (n > 0) {
      buf.append(tmp, (size_t)n);
      if (buf.size() > MAX_BLOB + 4) return false;  // garbage flood
      continue;
    }
    if (n < 0 && lan_would_block()) return true;
    return false;  // 0 = orderly close, <0 = error
  }
}

// A complete u32-framed blob at the front of buf? Extract into out.
bool take_frame(std::string &buf, std::string &out) {
  if (buf.size() < 4) return false;
  uint32_t len = (uint8_t)buf[0] | ((uint8_t)buf[1] << 8) |
                 ((uint8_t)buf[2] << 16) | ((uint32_t)(uint8_t)buf[3] << 24);
  if (len == 0 || len > MAX_BLOB) {
    buf.clear();  // poisoned stream
    return false;
  }
  if (buf.size() < 4 + (size_t)len) return false;
  out.assign(buf, 4, len);
  buf.erase(0, 4 + (size_t)len);
  return true;
}

std::string frame(const std::string &blob) {
  std::string f;
  uint32_t len = (uint32_t)blob.size();
  f.push_back((char)(len & 0xff));
  f.push_back((char)((len >> 8) & 0xff));
  f.push_back((char)((len >> 16) & 0xff));
  f.push_back((char)((len >> 24) & 0xff));
  f += blob;
  return f;
}

// Beacon destinations (network byte order): each broadcast-capable
// interface's DIRECTED broadcast (e.g. 192.168.1.255) plus the global
// broadcast and loopback. The global 255.255.255.255 alone is not
// enough: Wi-Fi routers commonly drop it between clients where the
// directed form gets through, and a multi-homed machine (VPN, virtual
// adapters) sends it out ONE interface of the OS's choosing — which is
// how a host can beacon into its VPN while the real LAN hears nothing.
// Recomputed every beacon so interface changes mid-lobby self-heal.
std::vector<uint32_t> beacon_dests() {
  std::vector<uint32_t> out;
  out.push_back(htonl(INADDR_BROADCAST));
  out.push_back(htonl(INADDR_LOOPBACK));
#ifdef _WIN32
  // SIO_GET_INTERFACE_LIST needs only ws2_32 (no iphlpapi link).
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s != INVALID_SOCKET) {
    INTERFACE_INFO ifl[32];
    DWORD got = 0;
    if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, nullptr, 0, ifl, sizeof(ifl),
                 &got, nullptr, nullptr) == 0) {
      int n = (int)(got / sizeof(INTERFACE_INFO));
      for (int i = 0; i < n; i++) {
        if (!(ifl[i].iiFlags & IFF_UP) || (ifl[i].iiFlags & IFF_LOOPBACK) ||
            !(ifl[i].iiFlags & IFF_BROADCAST))
          continue;
        uint32_t a = ((sockaddr_in *)&ifl[i].iiAddress)->sin_addr.s_addr;
        uint32_t m = ((sockaddr_in *)&ifl[i].iiNetmask)->sin_addr.s_addr;
        if (a && m) out.push_back(a | ~m);
      }
    }
    closesocket(s);
  }
#elif defined(__ANDROID__)
  // getifaddrs needs API 24 but minSdk is 21: the classic SIOCGIFCONF
  // walk works on every Android (IPv4-only, which is all beacons use).
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s >= 0) {
    struct ifreq reqs[16];
    struct ifconf conf;
    std::memset(&conf, 0, sizeof(conf));
    conf.ifc_len = sizeof(reqs);
    conf.ifc_req = reqs;
    if (ioctl(s, SIOCGIFCONF, &conf) == 0) {
      int n = (int)(conf.ifc_len / sizeof(struct ifreq));
      for (int i = 0; i < n; i++) {
        // The ioctls answer into the same union the name shares — work
        // on copies so one query can't corrupt the next.
        struct ifreq fl = reqs[i];
        if (ioctl(s, SIOCGIFFLAGS, &fl) != 0) continue;
        if (!(fl.ifr_flags & IFF_UP) || (fl.ifr_flags & IFF_LOOPBACK) ||
            !(fl.ifr_flags & IFF_BROADCAST))
          continue;
        struct ifreq br = reqs[i];
        if (ioctl(s, SIOCGIFBRDADDR, &br) != 0) continue;
        uint32_t b = ((sockaddr_in *)&br.ifr_broadaddr)->sin_addr.s_addr;
        if (b) out.push_back(b);
      }
    }
    ::close(s);
  }
#else
  ifaddrs *ifs = nullptr;
  if (getifaddrs(&ifs) == 0) {
    for (ifaddrs *p = ifs; p; p = p->ifa_next) {
      if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK) ||
          !(p->ifa_flags & IFF_BROADCAST))
        continue;
      if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
      if (!p->ifa_broadaddr) continue;
      uint32_t b = ((sockaddr_in *)p->ifa_broadaddr)->sin_addr.s_addr;
      if (b) out.push_back(b);
    }
    freeifaddrs(ifs);
  }
#endif
  // Dedupe (an interface's directed broadcast can repeat, and some
  // report 255.255.255.255 which is already first in the list).
  std::vector<uint32_t> uniq;
  for (size_t i = 0; i < out.size(); i++) {
    bool seen = false;
    for (size_t j = 0; j < uniq.size() && !seen; j++)
      seen = uniq[j] == out[i];
    if (!seen) uniq.push_back(out[i]);
  }
  return uniq;
}

std::string dests_to_string(const std::vector<uint32_t> &dests) {
  std::string s;
  for (size_t i = 0; i < dests.size(); i++) {
    in_addr a;
    a.s_addr = dests[i];
    char buf[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, &a, buf, sizeof(buf))) continue;
    if (!s.empty()) s += " ";
    s += buf;
  }
  return s;
}

}  // namespace

bool available() { return lan_sockets_init(); }

std::string local_host_name() {
  char buf[256] = {0};
  if (lan_sockets_init() && gethostname(buf, (int)sizeof(buf) - 1) == 0 &&
      buf[0]) {
    // Strip the domain suffix ("glenn-mbp.local" -> "glenn-mbp") and
    // uppercase into the game font's alphabet.
    std::string name(buf);
    size_t dot = name.find('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    for (size_t i = 0; i < name.size(); i++)
      name[i] = (char)toupper((unsigned char)name[i]);
    if (!name.empty()) return name.substr(0, MAX_NAME);
  }
  return "NEWTONIA";
}

// ---------------------------------------------------------------- Announce

struct Announce::Impl {
  lan_socket_t udp = LAN_INVALID;
  lan_socket_t listener = LAN_INVALID;
  lan_socket_t client = LAN_INVALID;
  uint16_t tcp_port = 0;
  std::string beacon;      // prebuilt packet
  int beacon_ms = 0;       // countdown to the next send
  std::string offer_blob;  // empty until gathering yields it
  std::string send_buf;    // framed offer in flight to the client
  size_t send_off = 0;
  bool offer_sent = false;
  std::string recv_buf;
  int client_ms = 0;  // time since the client connected (timeout)
  std::string dests_logged;  // last logged beacon-destination set
};

Announce::Announce() : impl_(new Impl()) {}
Announce::~Announce() {
  stop();
  delete impl_;
}
bool Announce::running() const { return impl_->udp != LAN_INVALID; }
bool Announce::peer_engaged() const { return impl_->client != LAN_INVALID; }
void Announce::set_offer_blob(const std::string &blob) {
  impl_->offer_blob = blob;
}

bool Announce::start(const std::string &host_name) {
  if (!lan_sockets_init()) return false;
  stop();
  Impl &im = *impl_;

  im.listener = socket(AF_INET, SOCK_STREAM, 0);
  if (im.listener == LAN_INVALID) return false;
  lan_set_nonblocking(im.listener);
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = 0;  // ephemeral: the beacon advertises it
  if (bind(im.listener, (sockaddr *)&addr, sizeof(addr)) != 0 ||
      listen(im.listener, 1) != 0) {
    stop();
    return false;
  }
  socklen_t alen = sizeof(addr);
  if (getsockname(im.listener, (sockaddr *)&addr, &alen) != 0) {
    stop();
    return false;
  }
  im.tcp_port = ntohs(addr.sin_port);

  im.udp = socket(AF_INET, SOCK_DGRAM, 0);
  if (im.udp == LAN_INVALID) {
    stop();
    return false;
  }
  lan_set_nonblocking(im.udp);
  int yes = 1;
  setsockopt(im.udp, SOL_SOCKET, SO_BROADCAST, (const char *)&yes,
             sizeof(yes));

  std::string name = host_name.substr(0, MAX_NAME);
  im.beacon.assign(MAGIC, 4);
  im.beacon.push_back((char)BEACON_VERSION);
  im.beacon.push_back((char)(Net::PROTO_VERSION & 0xff));
  im.beacon.push_back((char)((Net::PROTO_VERSION >> 8) & 0xff));
  im.beacon.push_back((char)(im.tcp_port & 0xff));
  im.beacon.push_back((char)((im.tcp_port >> 8) & 0xff));
  im.beacon.push_back((char)name.size());
  im.beacon += name;
  im.beacon_ms = 0;  // first beacon on the next update
  NET_LOG("net: lan announce up (tcp %d)\n", (int)im.tcp_port);
  return true;
}

void Announce::stop() {
  Impl &im = *impl_;
  if (im.udp != LAN_INVALID) lan_close(im.udp);
  if (im.listener != LAN_INVALID) lan_close(im.listener);
  if (im.client != LAN_INVALID) lan_close(im.client);
  im.udp = im.listener = im.client = LAN_INVALID;
  im.send_buf.clear();
  im.recv_buf.clear();
  im.send_off = 0;
  im.offer_sent = false;
}

bool Announce::update(int delta_ms, std::string &answer_out) {
  Impl &im = *impl_;
  if (im.udp == LAN_INVALID) return false;

  im.beacon_ms -= delta_ms;
  if (im.beacon_ms <= 0) {
    im.beacon_ms = BEACON_INTERVAL_MS;
    // Global broadcast + loopback (one-box tests) + every interface's
    // directed broadcast — see beacon_dests(). Recomputed per beacon so
    // plugging in a cable or dropping a VPN mid-lobby self-heals; the
    // set is logged whenever it changes (field diagnosis: the log shows
    // exactly which networks the host is beaconing into).
    std::vector<uint32_t> dests = beacon_dests();
    std::string dstr = dests_to_string(dests);
    if (dstr != im.dests_logged) {
      im.dests_logged = dstr;
      NET_LOG("net: lan beacon -> %s\n", dstr.c_str());
    }
    sockaddr_in to;
    std::memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(lan_port());
    for (size_t i = 0; i < dests.size(); i++) {
      to.sin_addr.s_addr = dests[i];
      sendto(im.udp, im.beacon.data(), (int)im.beacon.size(), 0,
             (sockaddr *)&to, sizeof(to));
    }
  }

  if (im.client == LAN_INVALID) {
    im.client = accept(im.listener, nullptr, nullptr);
    if (im.client == LAN_INVALID) return false;
    lan_set_nonblocking(im.client);
    im.client_ms = 0;
    im.send_off = 0;
    im.offer_sent = false;
    im.recv_buf.clear();
    NET_LOG("net: lan joiner connected\n");
  }

  im.client_ms += delta_ms;
  bool drop = im.client_ms > EXCHANGE_TIMEOUT_MS;

  // Serve the offer once gathering has minted it (the joiner waits).
  if (!drop && !im.offer_sent && !im.offer_blob.empty()) {
    if (im.send_buf.empty()) im.send_buf = frame(im.offer_blob);
    bool err = false;
    if (send_some(im.client, im.send_buf, im.send_off, err)) {
      im.offer_sent = true;
      NET_LOG("net: lan offer served (%d chars)\n", (int)im.offer_blob.size());
    } else if (err) {
      drop = true;
    }
  }

  if (!drop && im.offer_sent) {
    if (!recv_some(im.client, im.recv_buf)) {
      // Peer closed: only an error if the answer never arrived in full.
      if (!take_frame(im.recv_buf, answer_out)) drop = true;
      else {
        NET_LOG("net: lan answer received (%d chars)\n", (int)answer_out.size());
        return true;
      }
    } else if (take_frame(im.recv_buf, answer_out)) {
      NET_LOG("net: lan answer received (%d chars)\n", (int)answer_out.size());
      return true;
    }
  }

  if (drop) {
    NET_LOG("net: lan joiner dropped (timeout/error)\n");
    lan_close(im.client);
    im.client = LAN_INVALID;
    im.send_buf.clear();
    im.recv_buf.clear();
    im.send_off = 0;
    im.offer_sent = false;
  }
  return false;
}

// ------------------------------------------------------------------ Browse

struct Browse::Impl {
  lan_socket_t udp = LAN_INVALID;
  std::vector<HostInfo> hosts;
  lan_socket_t tcp = LAN_INVALID;
  enum { Idle, Connecting, WaitOffer, SendAnswer, Done, Error } state = Idle;
  int state_ms = 0;
  std::string recv_buf;
  std::string offer;
  bool offer_taken = false;
  std::string send_buf;
  size_t send_off = 0;
};

Browse::Browse() : impl_(new Impl()) {}
Browse::~Browse() {
  stop();
  delete impl_;
}
const std::vector<HostInfo> &Browse::hosts() const { return impl_->hosts; }
bool Browse::exchanging() const {
  return impl_->state != Impl::Idle && impl_->state != Impl::Error &&
         impl_->state != Impl::Done;
}
bool Browse::failed() const { return impl_->state == Impl::Error; }
bool Browse::running() const { return impl_->udp != LAN_INVALID; }

bool Browse::start() {
  if (!lan_sockets_init()) return false;
  stop();
  Impl &im = *impl_;
  im.udp = socket(AF_INET, SOCK_DGRAM, 0);
  if (im.udp == LAN_INVALID) return false;
  lan_set_nonblocking(im.udp);
  int yes = 1;
  setsockopt(im.udp, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes,
             sizeof(yes));
#ifdef SO_REUSEPORT
  setsockopt(im.udp, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes,
             sizeof(yes));
#endif
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(lan_port());
  if (bind(im.udp, (sockaddr *)&addr, sizeof(addr)) != 0) {
    stop();
    return false;
  }
  return true;
}

void Browse::stop() {
  Impl &im = *impl_;
  if (im.udp != LAN_INVALID) lan_close(im.udp);
  if (im.tcp != LAN_INVALID) lan_close(im.tcp);
  im.udp = im.tcp = LAN_INVALID;
  im.hosts.clear();
  im.state = Impl::Idle;
  im.recv_buf.clear();
  im.offer.clear();
  im.offer_taken = false;
  im.send_buf.clear();
  im.send_off = 0;
}

void Browse::update(int delta_ms) {
  Impl &im = *impl_;
  if (im.udp == LAN_INVALID) return;

  for (size_t i = 0; i < im.hosts.size(); i++) im.hosts[i].age_ms += delta_ms;

  // Drain beacons.
  for (;;) {
    char pkt[128];
    sockaddr_in from;
    socklen_t flen = sizeof(from);
    int n = (int)recvfrom(im.udp, pkt, (int)sizeof(pkt), 0, (sockaddr *)&from,
                          &flen);
    if (n <= 0) break;
    if (n < 10 || std::memcmp(pkt, MAGIC, 4) != 0) continue;
    if ((uint8_t)pkt[4] != BEACON_VERSION) continue;
    uint16_t proto = (uint8_t)pkt[5] | ((uint16_t)(uint8_t)pkt[6] << 8);
    uint16_t port = (uint8_t)pkt[7] | ((uint16_t)(uint8_t)pkt[8] << 8);
    size_t name_len = (uint8_t)pkt[9];
    if (name_len > MAX_NAME || 10 + name_len > (size_t)n) continue;
    HostInfo info;
    info.name.assign(pkt + 10, name_len);
    info.proto = proto;
    info.addr = from.sin_addr.s_addr;
    info.tcp_port = port;
    info.age_ms = 0;
    bool known = false;
    for (size_t i = 0; i < im.hosts.size(); i++) {
      // Same listener + name = the same host, whatever address the
      // beacon copy arrived from: a same-machine host is heard twice
      // (its broadcast copy under its LAN address AND its loopback
      // copy under 127.0.0.1) and must not draw two rows. Keep the
      // first-seen address — both reach the same TCP door.
      if (im.hosts[i].tcp_port == info.tcp_port &&
          im.hosts[i].name == info.name) {
        info.addr = im.hosts[i].addr;
        im.hosts[i] = info;
        known = true;
        break;
      }
    }
    if (!known && (int)im.hosts.size() < MAX_HOSTS) {
      NET_LOG("net: lan host found: %s (proto %d)\n", info.name.c_str(),
              (int)info.proto);
      im.hosts.push_back(info);
    }
  }

  // Expire silent hosts (~2 missed beacons; never mid-exchange — the
  // beacon legitimately stops once we engage the host's TCP door).
  if (im.state == Impl::Idle) {
    for (size_t i = 0; i < im.hosts.size();) {
      if (im.hosts[i].age_ms > HOST_EXPIRE_MS) {
        NET_LOG("net: lan host expired: %s\n", im.hosts[i].name.c_str());
        im.hosts.erase(im.hosts.begin() + i);
      } else {
        i++;
      }
    }
  }

  if (im.tcp == LAN_INVALID) return;
  im.state_ms += delta_ms;
  bool fail = im.state_ms > EXCHANGE_TIMEOUT_MS;

  if (!fail && im.state == Impl::Connecting) {
    // A non-blocking connect resolves via writability + SO_ERROR — the
    // one portable dance (a zero-byte send reads ENOTCONN mid-connect on
    // Linux; calling connect() again has per-platform errno soup).
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(im.tcp, &wfds);
    timeval tv = {0, 0};
    int r = select((int)(im.tcp + 1), nullptr, &wfds, nullptr, &tv);
    if (r > 0 && FD_ISSET(im.tcp, &wfds)) {
      int soerr = 0;
      socklen_t slen = sizeof(soerr);
      getsockopt(im.tcp, SOL_SOCKET, SO_ERROR, (char *)&soerr, &slen);
      if (soerr == 0) im.state = Impl::WaitOffer;
      else fail = true;
    } else if (r < 0) {
      fail = true;
    }
  }

  if (!fail && im.state == Impl::WaitOffer) {
    if (!recv_some(im.tcp, im.recv_buf)) fail = true;
    if (!fail && im.offer.empty() && take_frame(im.recv_buf, im.offer)) {
      NET_LOG("net: lan offer received (%d chars)\n", (int)im.offer.size());
    }
  }

  if (!fail && im.state == Impl::SendAnswer) {
    bool err = false;
    if (send_some(im.tcp, im.send_buf, im.send_off, err)) {
      NET_LOG("net: lan answer sent\n");
      im.state = Impl::Done;
      lan_close(im.tcp);
      im.tcp = LAN_INVALID;
    } else if (err) {
      fail = true;
    }
  }

  if (fail) {
    NET_LOG("net: lan exchange failed (state %d)\n", (int)im.state);
    lan_close(im.tcp);
    im.tcp = LAN_INVALID;
    im.state = Impl::Error;
  }
}

bool Browse::connect_host(int index) {
  Impl &im = *impl_;
  if (index < 0 || index >= (int)im.hosts.size()) return false;
  if (im.tcp != LAN_INVALID) return false;
  im.tcp = socket(AF_INET, SOCK_STREAM, 0);
  if (im.tcp == LAN_INVALID) return false;
  lan_set_nonblocking(im.tcp);
  sockaddr_in to;
  std::memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = im.hosts[index].addr;
  to.sin_port = htons(im.hosts[index].tcp_port);
  int r = connect(im.tcp, (sockaddr *)&to, sizeof(to));
  if (r != 0 && !lan_would_block()) {
    lan_close(im.tcp);
    im.tcp = LAN_INVALID;
    return false;
  }
  im.state = Impl::Connecting;
  im.state_ms = 0;
  im.recv_buf.clear();
  im.offer.clear();
  im.offer_taken = false;
  NET_LOG("net: lan joining %s\n", im.hosts[index].name.c_str());
  return true;
}

bool Browse::offer_ready(std::string &offer_out) {
  Impl &im = *impl_;
  if (im.offer.empty() || im.offer_taken) return false;
  offer_out = im.offer;
  im.offer_taken = true;
  return true;
}

void Browse::send_answer(const std::string &answer_blob) {
  Impl &im = *impl_;
  if (im.tcp == LAN_INVALID) return;
  im.send_buf = frame(answer_blob);
  im.send_off = 0;
  im.state = Impl::SendAnswer;
  im.state_ms = 0;
}

}  // namespace NetLan

#else  // !NEWTONIA_LAN — no-op seam (web/mobile/console)

namespace NetLan {

bool available() { return false; }
std::string local_host_name() { return "NEWTONIA"; }

struct Announce::Impl {};
Announce::Announce() : impl_(nullptr) {}
Announce::~Announce() {}
bool Announce::start(const std::string &) { return false; }
void Announce::stop() {}
void Announce::set_offer_blob(const std::string &) {}
bool Announce::update(int, std::string &) { return false; }
bool Announce::running() const { return false; }
bool Announce::peer_engaged() const { return false; }

struct Browse::Impl {};
Browse::Browse() : impl_(nullptr) {}
Browse::~Browse() {}
bool Browse::start() { return false; }
void Browse::stop() {}
void Browse::update(int) {}
static const std::vector<HostInfo> s_no_hosts;
const std::vector<HostInfo> &Browse::hosts() const { return s_no_hosts; }
bool Browse::connect_host(int) { return false; }
bool Browse::offer_ready(std::string &) { return false; }
void Browse::send_answer(const std::string &) {}
bool Browse::exchanging() const { return false; }
bool Browse::failed() const { return false; }
bool Browse::running() const { return false; }

}  // namespace NetLan

#endif  // NEWTONIA_LAN
