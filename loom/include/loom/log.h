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
#include <string>
#include <string_view>
#include <vector>

namespace loom::log {

// Numeric values match Python's logging levels.
enum class Level : int { Debug = 10, Info = 20, Warning = 30, Error = 40 };

std::string_view level_name(Level lvl) noexcept;  // "DEBUG", "INFO", "WARN", "ERROR"

struct Record {
  Level level = Level::Info;
  std::string logger;   // module name, e.g. "loom.db"
  std::string message;
  std::string time;     // local "HH:MM:SS"
  // "HH:MM:SS [INFO ] loom.db: message" (same shape as main.py's format)
  std::string formatted() const;
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

// Ring buffer of the last `capacity` formatted lines (default 500).
std::vector<std::string> recent(std::size_t max_lines = 500);
void clear_recent();
void set_ring_capacity(std::size_t capacity);

void write(Level lvl, std::string_view logger, std::string message);

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
