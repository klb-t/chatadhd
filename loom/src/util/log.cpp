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
#include <stdexcept>

#include "loom/util/time.h"

namespace loom::log {
namespace {

const RuntimeProfile& builtin_util() {
  static const RuntimeProfile profile = [] {
    auto result = RuntimeProfile::builtin("util");
    if (!result) throw std::logic_error(result.error().to_string());
    return std::move(*result);
  }();
  return profile;
}

Result<RuntimeProfile> checked_util(const RuntimeProfile& profile) {
  if (profile.domain() != "util") return Error(Errc::InvalidArgument, "expected util profile");
  return builtin_util().with_values(profile.values());
}

const std::string& policy_level_name(Level lvl, const Json& values) {
  const auto* name = json::find(values.at("level_names"), std::to_string(static_cast<int>(lvl)));
  return (name ? *name : values.at("default_level_name")).get_ref<const std::string&>();
}

Result<std::string> render_record(const Record& record, const Json& values) {
  std::string name = policy_level_name(record.level, values);
  const auto width = values.at("level_width").get<std::size_t>();
  if (name.size() < width) name.append(width - name.size(), ' ');
  return render_profile_template(values.at("record_template").get_ref<const std::string&>(),
      Json{{"time", record.time}, {"level", name}, {"logger", record.logger}, {"message", record.message}});
}

Level level_from_env(const Json& values) {
  const auto default_level = static_cast<Level>(values.at("default_level").get<int>());
  const char* v = std::getenv(values.at("level_environment").get_ref<const std::string&>().c_str());
  if (!v) return default_level;
  std::string s(v);
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const auto* configured = json::find(values.at("level_aliases"), s);
  return configured ? static_cast<Level>(configured->get<int>()) : default_level;
}

bool stderr_from_env(const Json& values) {
  const char* v = std::getenv(values.at("stderr_environment").get_ref<const std::string&>().c_str());
  return v ? std::string(v) != values.at("stderr_disabled_value").get_ref<const std::string&>()
           : values.at("stderr_enabled").get<bool>();
}

struct State {
  std::mutex mu;
  std::map<int, std::shared_ptr<Sink>> sinks;
  int next_token = 1;
  std::deque<std::string> ring;
  std::size_t ring_capacity = builtin_util().values().at("log").at("ring_capacity").get<std::size_t>();
  std::mutex stderr_mu;
};

State& state() {
  static State s;
  return s;
}

std::atomic<int> g_level{static_cast<int>(level_from_env(builtin_util().values().at("log")))};
std::atomic<bool> g_stderr{stderr_from_env(builtin_util().values().at("log"))};

void publish_record(const Record& rec, const std::string& line) {
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
      // A broken sink must never take the process down; preserve legacy drop.
    }
  }
}

}  // namespace

std::string_view level_name(Level lvl) noexcept {
  return policy_level_name(lvl, builtin_util().values().at("log"));
}

Result<std::string> level_name(Level lvl, const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto checked, checked_util(profile));
  return policy_level_name(lvl, checked.values().at("log"));
}

std::string Record::formatted() const {
  auto rendered = render_record(*this, builtin_util().values().at("log"));
  if (!rendered) throw std::logic_error(rendered.error().to_string());
  return std::move(*rendered);
}

Result<std::string> Record::formatted_checked(const RuntimeProfile& profile) const {
  LOOM_TRY_ASSIGN(auto checked, checked_util(profile));
  return render_record(*this, checked.values().at("log"));
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

Result<Level> preset_level(const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto checked, checked_util(profile));
  return level_from_env(checked.values().at("log"));
}

Result<bool> preset_stderr(const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto checked, checked_util(profile));
  return stderr_from_env(checked.values().at("log"));
}

std::vector<std::string> recent(std::optional<std::size_t> requested_max_lines) {
  const auto max_lines = requested_max_lines.value_or(builtin_util().values().at("log").at("recent_lines").get<std::size_t>());
  auto& s = state();
  std::lock_guard lk(s.mu);
  std::size_t n = std::min(max_lines, s.ring.size());
  return {s.ring.end() - static_cast<std::ptrdiff_t>(n), s.ring.end()};
}

std::vector<std::string> recent(std::size_t max_lines) {
  return recent(std::optional<std::size_t>(max_lines));
}

Result<std::vector<std::string>> recent_checked(const RuntimeProfile& profile,
                                               std::optional<std::size_t> max_lines) {
  LOOM_TRY_ASSIGN(auto checked, checked_util(profile));
  return recent(max_lines.value_or(checked.values().at("log").at("recent_lines").get<std::size_t>()));
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

Status set_ring_capacity_checked(const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto checked, checked_util(profile));
  auto& s = state();
  std::lock_guard lk(s.mu);
  s.ring_capacity = checked.values().at("log").at("ring_capacity").get<std::size_t>();
  while (s.ring.size() > s.ring_capacity) s.ring.pop_front();
  return {};
}

void write(Level lvl, std::string_view logger, std::string message) {
  if (!enabled(lvl)) return;
  Record rec{lvl, std::string(logger), std::move(message), timeutil::local_clock_hms()};
  publish_record(rec, rec.formatted());
}

Status write_checked(Level lvl, std::string_view logger, std::string message, const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto checked, checked_util(profile));
  if (!enabled(lvl)) return {};
  Record rec{lvl, std::string(logger), std::move(message), timeutil::local_clock_hms()};
  LOOM_TRY_ASSIGN(auto line, render_record(rec, checked.values().at("log")));
  publish_record(rec, line);
  return {};
}

}  // namespace loom::log
