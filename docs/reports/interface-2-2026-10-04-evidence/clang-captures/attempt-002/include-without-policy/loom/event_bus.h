// loom/event_bus.h — port of engine/events.py (EventBus).
//
// Synchronous pub/sub: handlers run in the emitter's thread (same as Python).
// Differences from Python, all additive:
//   * one bus per Runtime instead of a module singleton;
//   * on() returns a SubscriptionId used by off() (Python passed the handler);
//   * "*" subscribes to every event (used by the C API loom_subscribe);
//   * optional persistence of selected event types into the append-only
//     EventLog (policy: config key "loom_event_log_types").
// Thread safety: the registry is locked only while copying the handler list;
// handlers are invoked outside the lock, so a handler may call on()/off()/
// emit() re-entrantly. A handler removed during an emit may still receive
// that in-flight event. Exceptions from handlers are caught and logged.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/util/json.h"

namespace loom {

class EventLog;

// Event names (engine/events.py constants + semantic_worker.SEMANTIC_PROGRESS).
namespace events {
inline constexpr std::string_view kMsgCreated = "message:created";
inline constexpr std::string_view kMsgUpdated = "message:updated";
inline constexpr std::string_view kNodeCreated = "node:created";
inline constexpr std::string_view kEdgeCreated = "edge:created";
inline constexpr std::string_view kConvCreated = "conv:created";
inline constexpr std::string_view kConvSwitched = "conv:switched";
inline constexpr std::string_view kGraphChanged = "graph:changed";
inline constexpr std::string_view kImportDone = "import:done";
inline constexpr std::string_view kSemanticProgress = "semantic:progress";
// Loom additions.
inline constexpr std::string_view kTaskChanged = "task:changed";    // {id, kind, status, ...}
inline constexpr std::string_view kTaskProgress = "task:progress";  // {id, current, total, status}
inline constexpr std::string_view kImportProgress = "import:progress";
inline constexpr std::string_view kWildcard = "*";
}  // namespace events

class EventBus {
 public:
  using Handler = std::function<void(std::string_view event, const Json& data)>;
  using SubscriptionId = std::uint64_t;

  EventBus() = default;
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  // Subscribe to `event` ("*" = all events). Never returns 0.
  SubscriptionId on(std::string_view event, Handler handler);
  // Returns false if the id is unknown (Python off() silently ignored that).
  bool off(SubscriptionId id);
  // Emit synchronously in the calling thread. `data` defaults to null
  // (Python emit(event, data=None)).
  void emit(std::string_view event, const Json& data = nullptr);

  std::size_t handler_count(std::string_view event) const;

  // Persist events whose type is in `types` (exact names; "*" = all;
  // a trailing ":*" matches a prefix, e.g. "task:*") into `log`.
  // Pass nullptr to stop persisting.
  void set_persistence(EventLog* log, std::vector<std::string> types);

 private:
  struct Entry {
    SubscriptionId id;
    std::string event;
    std::shared_ptr<Handler> fn;
  };
  bool should_persist(std::string_view event) const;

  mutable std::mutex mu_;
  std::vector<Entry> entries_;
  SubscriptionId next_id_ = 1;
  EventLog* log_ = nullptr;
  std::vector<std::string> persist_types_;
};

// RAII subscription: unsubscribes on destruction.
class ScopedSubscription {
 public:
  ScopedSubscription() = default;
  ScopedSubscription(EventBus& bus, EventBus::SubscriptionId id) : bus_(&bus), id_(id) {}
  ~ScopedSubscription() { reset(); }
  ScopedSubscription(ScopedSubscription&& o) noexcept : bus_(o.bus_), id_(o.id_) { o.bus_ = nullptr; }
  ScopedSubscription& operator=(ScopedSubscription&& o) noexcept {
    if (this != &o) {
      reset();
      bus_ = o.bus_;
      id_ = o.id_;
      o.bus_ = nullptr;
    }
    return *this;
  }
  ScopedSubscription(const ScopedSubscription&) = delete;
  ScopedSubscription& operator=(const ScopedSubscription&) = delete;
  void reset() {
    if (bus_) bus_->off(id_);
    bus_ = nullptr;
  }
  EventBus::SubscriptionId id() const noexcept { return id_; }

 private:
  EventBus* bus_ = nullptr;
  EventBus::SubscriptionId id_ = 0;
};

}  // namespace loom
