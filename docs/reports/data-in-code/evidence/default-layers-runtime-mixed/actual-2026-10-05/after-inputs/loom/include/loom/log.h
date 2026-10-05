// loom/log.h — process-wide logger with pluggable sinks.
//
// Port of the Python logging setup (main.py basicConfig) plus gui/base.py
// LOGBUF: every record goes to all installed sinks and into a ring buffer of
// the last 500 formatted lines (for in-app log panels). The default stderr sink
// can be disabled; platforms install their own sink (Android: logcat through
// loom_set_log_sink).
#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/runtime_profile.h"

namespace loom::log {

// Numeric values match Python's logging levels.
enum class Level : int { Debug = 10, Info = 20, Warning = 30, Error = 40 };

std::string_view level_name(Level lvl) noexcept;  // "DEBUG", "INFO", "WARN", "ERROR"
Result<std::string> level_name(Level lvl, const RuntimeProfile& profile);

struct Record {
  Level level = Level::Info;
  std::string logger;   // module name, e.g. "loom.db"
  std::string message;
  std::string time;     // local "HH:MM:SS"
  // "HH:MM:SS [INFO ] loom.db: message" (same shape as main.py's format)
  std::string formatted() const;
  // Inert per-record renderer; validates the util consumer contract and
  // preserves missing-template-variable errors without installing global data.
  Result<std::string> formatted_checked(const RuntimeProfile& profile) const;
};

using Sink = std::function<void(const Record&)>;

// Sinks: returns a token for remove_sink(). Sinks may be called from any
// thread; they are invoked outside the registry lock, one record at a time.
int add_sink(Sink sink);
void remove_sink(int token);

void set_level(Level lvl);     // global threshold (default Info, or LOOM_LOG_LEVEL env)
Level level();
bool enabled(Level lvl);
void set_stderr(bool on);      // built-in stderr sink (default on, LOOM_LOG_STDERR=0 disables)
// Resolve initialization presets plus environment through caller-owned data.
// The caller applies the existing explicit setters; no profile is globalized.
Result<Level> preset_level(const RuntimeProfile& profile);
Result<bool> preset_stderr(const RuntimeProfile& profile);

// Ring buffer of the last `capacity` formatted lines (default 500).
std::vector<std::string> recent(std::optional<std::size_t> max_lines = std::nullopt);
std::vector<std::string> recent(std::size_t max_lines);  // preserve the pre-profile exported symbol
Result<std::vector<std::string>> recent_checked(const RuntimeProfile& profile,
                                                std::optional<std::size_t> max_lines = std::nullopt);
void clear_recent();
void set_ring_capacity(std::size_t capacity);
// Applies only the explicit scalar setting to the existing ring. Profile zero
// disables retention; the historical size_t setter keeps zero -> one.
Status set_ring_capacity_checked(const RuntimeProfile& profile);

void write(Level lvl, std::string_view logger, std::string message);
Status write_checked(Level lvl, std::string_view logger, std::string message, const RuntimeProfile& profile);

template <class... Args>
void debug(std::string_view logger, std::format_string<Args...> fmt, Args&&... args) {
  if (enabled(Level::Debug)) write(Level::Debug, logger, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::string_view logger, std::format_string<Args...> fmt, Args&&... args) {
  if (enabled(Level::Info)) write(Level::Info, logger, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::string_view logger, std::format_string<Args...> fmt, Args&&... args) {
  if (enabled(Level::Warning)) write(Level::Warning, logger, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::string_view logger, std::format_string<Args...> fmt, Args&&... args) {
  if (enabled(Level::Error)) write(Level::Error, logger, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace loom::log
