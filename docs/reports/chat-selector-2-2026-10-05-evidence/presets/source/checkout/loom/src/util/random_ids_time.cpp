// OS randomness, ID generation and time formatting.
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/random.h>
#endif

#include "loom/util/ids.h"
#include "loom/util/random.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom {

void random_bytes(void* out, std::size_t len) noexcept {
  auto* p = static_cast<unsigned char*>(out);
#if defined(__APPLE__)
  arc4random_buf(p, len);
  return;
#else
#if defined(__linux__) || defined(__ANDROID__)
  while (len > 0) {
    ssize_t got = ::getrandom(p, len, 0);
    if (got < 0) break;  // fall through to /dev/urandom
    p += got;
    len -= static_cast<std::size_t>(got);
  }
  if (len == 0) return;
#endif
  int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) std::abort();
  while (len > 0) {
    ssize_t got = ::read(fd, p, len);
    if (got <= 0) std::abort();
    p += got;
    len -= static_cast<std::size_t>(got);
  }
  ::close(fd);
#endif
}

std::string random_bytes(std::size_t len) {
  std::string s(len, '\0');
  random_bytes(s.data(), len);
  return s;
}

std::uint64_t random_u64() noexcept {
  std::uint64_t v = 0;
  random_bytes(&v, sizeof v);
  return v;
}

std::string random_hex(std::size_t n_chars) {
  std::string bytes = random_bytes((n_chars + 1) / 2);
  std::string hex = to_hex(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
  hex.resize(n_chars);
  return hex;
}

std::string gen_id(std::string_view prefix) {
  std::string id(prefix);
  id += random_hex(12);
  return id;
}

bool is_generated_id(std::string_view id, std::string_view prefix) noexcept {
  if (id.size() != prefix.size() + 12 || id.substr(0, prefix.size()) != prefix) return false;
  for (char c : id.substr(prefix.size())) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

namespace timeutil {

std::string format_iso_utc(Clock::time_point tp) {
  using namespace std::chrono;
  auto us_total = duration_cast<microseconds>(tp.time_since_epoch()).count();
  auto secs = us_total / 1'000'000;
  auto us = us_total % 1'000'000;
  if (us < 0) {
    us += 1'000'000;
    secs -= 1;
  }
  std::time_t t = static_cast<std::time_t>(secs);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[80];
  if (us == 0) {
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  } else {
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%06lldZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long long>(us));
  }
  return buf;
}

std::string utc_now_iso() { return format_iso_utc(Clock::now()); }

std::optional<Clock::time_point> parse_iso_utc(std::string_view s) {
  int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
  if (s.size() < 19) return std::nullopt;
  std::string head(s.substr(0, 19));
  if (std::sscanf(head.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &sec) != 6) {
    if (std::sscanf(head.c_str(), "%4d-%2d-%2d %2d:%2d:%2d", &Y, &M, &D, &h, &m, &sec) != 6) return std::nullopt;
  }
  long long us = 0;
  std::size_t i = 19;
  if (i < s.size() && s[i] == '.') {
    ++i;
    int digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      if (digits < 6) {
        us = us * 10 + (s[i] - '0');
        ++digits;
      }
      ++i;
    }
    while (digits++ < 6) us *= 10;
  }
  std::tm tm{};
  tm.tm_year = Y - 1900;
  tm.tm_mon = M - 1;
  tm.tm_mday = D;
  tm.tm_hour = h;
  tm.tm_min = m;
  tm.tm_sec = sec;
  std::time_t t = timegm(&tm);
  return Clock::time_point(std::chrono::seconds(t)) + std::chrono::microseconds(us);
}

std::string local_now_format(const char* strftime_fmt) {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[128];
  std::size_t n = std::strftime(buf, sizeof buf, strftime_fmt, &tm);
  return std::string(buf, n);
}

std::string local_clock_hms() { return local_now_format("%H:%M:%S"); }

std::int64_t unix_millis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

double monotonic_seconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace timeutil
}  // namespace loom
