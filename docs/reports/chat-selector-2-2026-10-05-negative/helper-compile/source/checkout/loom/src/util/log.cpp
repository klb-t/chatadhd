#include "loom/log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>

#include "loom/util/time.h"

namespace loom::log {
namespace {

Level level_from_env() {
  const char* v = std::getenv("LOOM_LOG_LEVEL");
  if (!v) return Level::Info;
  std::string s(v);
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (s == "debug") return Level::Debug;
  if (s == "warning" || s == "warn") return Level::Warning;
  if (s == "error") return Level::Error;
  return Level::Info;
}

bool stderr_from_env() {
  const char* v = std::getenv("LOOM_LOG_STDERR");
  return !(v && std::string(v) == "0");
}

struct State {
  std::mutex mu;
  std::map<int, std::shared_ptr<Sink>> sinks;
  int next_token = 1;
  std::deque<std::string> ring;
  std::size_t ring_capacity = 500;
  std::mutex stderr_mu;
};

State& state() {
  static State s;
  return s;
}

std::atomic<int> g_level{static_cast<int>(level_from_env())};
std::atomic<bool> g_stderr{stderr_from_env()};

}  // namespace

std::string_view level_name(Level lvl) noexcept {
  switch (lvl) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warning: return "WARN";
    case Level::Error: return "ERROR";
  }
  return "INFO";
}

std::string Record::formatted() const {
  std::string lvl(level_name(level));
  while (lvl.size() < 5) lvl.push_back(' ');
  std::string out;
  out.reserve(time.size() + lvl.size() + logger.size() + message.size() + 8);
  out += time;
  out += " [";
  out += lvl;
  out += "] ";
  out += logger;
  out += ": ";
  out += message;
  return out;
}

int add_sink(Sink sink) {
  auto& s = state();
  std::lock_guard lk(s.mu);
  int tok = s.next_token++;
  s.sinks.emplace(tok, std::make_shared<Sink>(std::move(sink)));
  return tok;
}

void remove_sink(int token) {
  auto& s = state();
  std::lock_guard lk(s.mu);
  s.sinks.erase(token);
}

void set_level(Level lvl) { g_level.store(static_cast<int>(lvl)); }
Level level() { return static_cast<Level>(g_level.load()); }
bool enabled(Level lvl) { return static_cast<int>(lvl) >= g_level.load(std::memory_order_relaxed); }
void set_stderr(bool on) { g_stderr.store(on); }

std::vector<std::string> recent(std::size_t max_lines) {
  auto& s = state();
  std::lock_guard lk(s.mu);
  std::size_t n = std::min(max_lines, s.ring.size());
  return {s.ring.end() - static_cast<std::ptrdiff_t>(n), s.ring.end()};
}

void clear_recent() {
  auto& s = state();
  std::lock_guard lk(s.mu);
  s.ring.clear();
}

void set_ring_capacity(std::size_t capacity) {
  auto& s = state();
  std::lock_guard lk(s.mu);
  s.ring_capacity = capacity == 0 ? 1 : capacity;
  while (s.ring.size() > s.ring_capacity) s.ring.pop_front();
}

void write(Level lvl, std::string_view logger, std::string message) {
  if (!enabled(lvl)) return;
  Record rec{lvl, std::string(logger), std::move(message), timeutil::local_clock_hms()};
  std::string line = rec.formatted();
  std::vector<std::shared_ptr<Sink>> sinks;
  auto& s = state();
  {
    std::lock_guard lk(s.mu);
    s.ring.push_back(line);
    while (s.ring.size() > s.ring_capacity) s.ring.pop_front();
    sinks.reserve(s.sinks.size());
    for (auto& [tok, sink] : s.sinks) sinks.push_back(sink);
  }
  if (g_stderr.load()) {
    std::lock_guard lk(s.stderr_mu);
    std::fprintf(stderr, "%s\n", line.c_str());
  }
  for (auto& sink : sinks) {
    try {
      (*sink)(rec);
    } catch (...) {
      // A broken sink must never take the process down; nothing sensible to
      // log here (we are the logger), so the record is dropped for this sink.
    }
  }
}

}  // namespace loom::log
