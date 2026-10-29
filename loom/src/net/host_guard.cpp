#include "loom/net/host_guard.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace loom::net {
namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

// scheme://[userinfo@]host[:port]/path -> host (brackets of an IPv6 literal removed)
std::string host_of(std::string_view url) {
  const auto scheme = url.find("://");
  if (scheme == std::string_view::npos) return {};
  std::string_view rest = url.substr(scheme + 3);
  const auto end = rest.find_first_of("/?#");
  std::string_view auth = rest.substr(0, end);
  if (const auto at = auth.rfind('@'); at != std::string_view::npos) auth = auth.substr(at + 1);
  if (!auth.empty() && auth.front() == '[') {
    const auto close = auth.find(']');
    return close == std::string_view::npos ? std::string() : std::string(auth.substr(1, close - 1));
  }
  if (const auto colon = auth.find(':'); colon != std::string_view::npos) auth = auth.substr(0, colon);
  return std::string(auth);
}

#if !defined(_WIN32)
bool v4_non_public(const unsigned char* b) {
  if (b[0] == 0 || b[0] == 10 || b[0] == 127) return true;
  if (b[0] == 169 && b[1] == 254) return true;
  if (b[0] == 172 && b[1] >= 16 && b[1] <= 31) return true;
  if (b[0] == 192 && b[1] == 168) return true;
  if (b[0] == 100 && b[1] >= 64 && b[1] <= 127) return true;  // carrier-grade NAT
  if (b[0] >= 224) return true;                                // multicast, reserved, broadcast
  return false;
}
bool v6_non_public(const unsigned char* b) {
  static const unsigned char zero[10] = {0};
  if (std::memcmp(b, zero, 10) == 0 && b[10] == 0xff && b[11] == 0xff) return v4_non_public(b + 12);  // ::ffff:a.b.c.d
  bool all_zero_but_last = true;
  for (int i = 0; i < 15; ++i) all_zero_but_last = all_zero_but_last && b[i] == 0;
  if (all_zero_but_last && (b[15] == 0 || b[15] == 1)) return true;  // :: and ::1
  if ((b[0] & 0xfe) == 0xfc) return true;                             // fc00::/7
  if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return true;             // fe80::/10
  if (b[0] == 0xff) return true;                                      // multicast
  return false;
}
#endif

}  // namespace

bool is_non_public_address(std::string_view ip) {
#if defined(_WIN32)
  (void)ip;
  return false;
#else
  const std::string s(ip);
  unsigned char buf[16];
  if (inet_pton(AF_INET, s.c_str(), buf) == 1) return v4_non_public(buf);
  if (inet_pton(AF_INET6, s.c_str(), buf) == 1) return v6_non_public(buf);
  return false;
#endif
}

Status check_listed_host(std::string_view url, const std::vector<std::string>& hosts) {
  if (hosts.empty()) return {};
  const std::string host = lower(host_of(url));
  if (host.empty()) return {};
  for (const auto& h : hosts) {
    const std::string a = lower(h);
    if (a.empty()) continue;
    if (host == a || (host.size() > a.size() && host.compare(host.size() - a.size(), a.size(), a) == 0 &&
                      host[host.size() - a.size() - 1] == '.'))
      return {};
  }
  return Error(Errc::InvalidArgument, "a session without an account connects only to the hosts of the listed model providers ('" + host +
                                          "' is not one); the operator can add a host in the server_accounts profile (guests.protection.outbound)");
}

Status check_public_url(std::string_view url, const std::vector<std::string>& allow_hosts) {
#if defined(_WIN32)
  (void)url;
  (void)allow_hosts;
  return {};
#else
  const std::string host = lower(host_of(url));
  if (host.empty()) return {};  // not a network URL; the transport reports it
  for (const auto& a : allow_hosts)
    if (lower(a) == host) return {};
  const std::string refuse = "this server does not connect to private or local network addresses ('" + host +
                             "'); the operator can allow a host in the server_accounts profile (allow_private_hosts)";
  if (host == "localhost" || (host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0))
    return Error(Errc::InvalidArgument, refuse);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return {};  // unresolvable: the transport fails on its own
  bool bad = false;
  for (addrinfo* p = res; p; p = p->ai_next) {
    if (p->ai_family == AF_INET) {
      bad = bad || v4_non_public(reinterpret_cast<const unsigned char*>(&reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr));
    } else if (p->ai_family == AF_INET6) {
      bad = bad || v6_non_public(reinterpret_cast<const unsigned char*>(&reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr));
    }
  }
  freeaddrinfo(res);
  if (bad) return Error(Errc::InvalidArgument, refuse);
  return {};
#endif
}

}  // namespace loom::net
