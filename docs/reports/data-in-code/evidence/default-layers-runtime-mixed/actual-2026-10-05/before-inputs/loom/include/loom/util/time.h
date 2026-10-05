// loom/util/time.h — timestamps with Python-compatible formatting.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace loom::timeutil {

using Clock = std::chrono::system_clock;

// Python: datetime.utcnow().isoformat() + "Z"
//   "2026-09-25T17:14:03.123456Z", or "2026-09-25T17:14:03Z" when the
//   microsecond field is exactly 0 (isoformat omits it).
std::string utc_now_iso();
std::string format_iso_utc(Clock::time_point tp);

// Parses the formats above (with or without fraction/"Z", or "+00:00").
std::optional<Clock::time_point> parse_iso_utc(std::string_view s);

// Python f"{datetime.now():<fmt>}" — local time through strftime.
std::string local_now_format(const char* strftime_fmt);
// Local "HH:MM:SS" (log lines).
std::string local_clock_hms();

std::int64_t unix_millis();
// Monotonic seconds (Python time.monotonic()).
double monotonic_seconds();

}  // namespace loom::timeutil
