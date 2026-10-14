// Durable storage of action runs: checkpoints are append-only events (type "action.run", subject = run id),
// so the audit trail and the resumable state are the same record and no schema changes.
#include <algorithm>
#include <map>

#include "loom/action_run.h"
#include "loom/provenance.h"

namespace loom::actions {

namespace {

constexpr std::string_view kEventType = "action.run";
constexpr std::string_view kIndexType = "action.seq_index";
constexpr std::string_view kIndexSubject = "sequence";

class EventLogRunStore final : public RunStore {
 public:
  explicit EventLogRunStore(EventLog& log) : log_(log) {}

  Status save(const Json& run) override {
    const std::string id = json::get_string(run, "id");
    if (id.empty()) return Error(Errc::InvalidArgument, "a run needs an id");
    LOOM_TRY_ASSIGN(auto seq, log_.append(kEventType, id, run));
    (void)seq;
    return {};
  }

  Result<std::optional<Json>> load(std::string_view run_id) override {
    EventQuery q;
    q.type = std::string(kEventType);
    q.subject_id = std::string(run_id);
    q.limit = 1'000'000;
    LOOM_TRY_ASSIGN(auto events, log_.query(q));
    if (events.empty()) return std::optional<Json>{};
    return std::optional<Json>(events.back().payload);
  }

  Result<std::vector<Checkpoint>> checkpoints_since(std::int64_t after_seq, std::size_t limit) override {
    EventQuery q;
    q.type = std::string(kEventType);
    q.after_seq = after_seq;
    q.limit = static_cast<int>(limit);
    LOOM_TRY_ASSIGN(auto events, log_.query(q));
    std::vector<Checkpoint> out;
    out.reserve(events.size());
    for (auto& e : events) out.push_back(Checkpoint{e.seq, std::move(e.payload)});
    return out;
  }

  // Snapshots are events of their own type; the newest is found by looking back from the end of the log, a wider window each time,
  // so that a restart does not read the whole log to find a record that is nearly always among the last few thousand events.
  Result<std::optional<Json>> load_index() override {
    LOOM_TRY_ASSIGN(auto last, log_.last_seq());
    for (std::int64_t window = 4096;; window *= 8) {
      EventQuery q;
      q.type = std::string(kIndexType);
      q.subject_id = std::string(kIndexSubject);
      q.after_seq = std::max<std::int64_t>(0, last - window);
      q.limit = 1'000'000;
      LOOM_TRY_ASSIGN(auto events, log_.query(q));
      if (!events.empty()) return std::optional<Json>(events.back().payload);
      if (q.after_seq == 0) return std::optional<Json>{};
    }
  }

  Status save_index(const Json& snapshot) override {
    LOOM_TRY_ASSIGN(auto seq, log_.append(kIndexType, kIndexSubject, snapshot));
    (void)seq;
    return {};
  }

  Result<std::vector<Json>> list(const Json& filter) override {
    EventQuery q;
    q.type = std::string(kEventType);
    q.limit = 1'000'000;
    LOOM_TRY_ASSIGN(auto events, log_.query(q));
    std::map<std::string, std::size_t> latest;  // run id -> index of its last checkpoint
    for (std::size_t i = 0; i < events.size(); ++i) latest[events[i].subject_id] = i;
    std::vector<std::size_t> order;
    for (const auto& [id, index] : latest) order.push_back(index);
    std::sort(order.begin(), order.end(), std::greater<>());  // newest checkpoint first
    const std::string status = json::get_string(filter, "status");
    const std::string conv = json::get_string(filter, "conv_id");
    const std::int64_t limit = json::get_int(filter, "limit", 100);
    std::vector<Json> out;
    for (std::size_t index : order) {
      const Json& doc = events[index].payload;
      if (!status.empty() && json::get_string(doc, "status") != status) continue;
      if (!conv.empty() && json::get_string(doc.value("request", Json::object()).value("context", Json::object()), "conv_id") != conv) continue;
      out.push_back(doc);
      if (limit > 0 && static_cast<std::int64_t>(out.size()) >= limit) break;
    }
    return out;
  }

 private:
  EventLog& log_;
};

}  // namespace

std::unique_ptr<RunStore> make_event_log_run_store(EventLog& log) { return std::make_unique<EventLogRunStore>(log); }

}  // namespace loom::actions
