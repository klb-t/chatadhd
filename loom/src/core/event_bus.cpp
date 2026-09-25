#include "loom/event_bus.h"

#include <exception>

#include "loom/log.h"
#include "loom/provenance.h"

namespace loom {
namespace {
constexpr std::string_view kLog = "loom.events";

bool type_matches(std::string_view pattern, std::string_view event) {
  if (pattern == events::kWildcard) return true;
  if (pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == ":*") {
    return event.substr(0, pattern.size() - 1) == pattern.substr(0, pattern.size() - 1);
  }
  return pattern == event;
}
}  // namespace

EventBus::SubscriptionId EventBus::on(std::string_view event, Handler handler) {
  std::lock_guard lk(mu_);
  SubscriptionId id = next_id_++;
  entries_.push_back(Entry{id, std::string(event), std::make_shared<Handler>(std::move(handler))});
  return id;
}

bool EventBus::off(SubscriptionId id) {
  std::lock_guard lk(mu_);
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->id == id) {
      entries_.erase(it);
      return true;
    }
  }
  return false;
}

std::size_t EventBus::handler_count(std::string_view event) const {
  std::lock_guard lk(mu_);
  std::size_t n = 0;
  for (const auto& e : entries_) {
    if (e.event == event) ++n;
  }
  return n;
}

void EventBus::set_persistence(EventLog* log, std::vector<std::string> types) {
  std::lock_guard lk(mu_);
  log_ = log;
  persist_types_ = std::move(types);
}

bool EventBus::should_persist(std::string_view event) const {
  for (const auto& p : persist_types_) {
    if (type_matches(p, event)) return true;
  }
  return false;
}

void EventBus::emit(std::string_view event, const Json& data) {
  std::vector<std::shared_ptr<Handler>> targets;
  EventLog* log = nullptr;
  {
    std::lock_guard lk(mu_);
    targets.reserve(entries_.size());
    for (const auto& e : entries_) {
      if (e.event == event || e.event == events::kWildcard) targets.push_back(e.fn);
    }
    if (log_ && should_persist(event)) log = log_;
  }
  if (log) {
    std::string subject;
    if (data.is_object()) {
      if (const Json* id = json::find(data, "id"); id && id->is_string()) subject = id->get<std::string>();
    }
    auto r = log->append(event, subject, data.is_object() ? data : Json{{"data", data}});
    if (!r) log::warn(kLog, "EventLog append failed for {}: {}", event, r.error().message);
  }
  for (const auto& fn : targets) {
    try {
      (*fn)(event, data);
    } catch (const std::exception& e) {
      log::error(kLog, "Event handler error for {}: {}", event, e.what());
    } catch (...) {
      log::error(kLog, "Event handler error for {}: unknown exception", event);
    }
  }
}

}  // namespace loom
