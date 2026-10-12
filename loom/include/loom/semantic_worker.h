// loom/semantic_worker.h — port of engine/semantic_worker.py. [OWNER: wave 2 net/chat/worker]
//
// Background thread draining messages with semantic_status='pending'.
// Per-message analysis claims a durable executing state before dispatch and
// atomically completes graph writes + done, or records failed with evidence.
// Failed/interrupted attempts require explicit requeue through update_msg;
// pause/resume is only worker scheduling, never permission to retry.
// Existing batch helpers are separate: active batch identity is process-local,
// and durable batch polling/recovery is not implemented by this worker.
// stop() is interruptible; drain_once() permits deterministic runtime tests.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>

#include "loom/analyzer_binding.h"
#include "loom/event_bus.h"
#include "loom/result.h"
#include "loom/runtime_profile.h"
#include "loom/util/json.h"

namespace loom {

class Database;
class SemanticLLM;
class GraphEngine;
class Config;
class Secrets;
class SemanticAnalyzer;
class TaskEngine;
namespace net {
class HttpTransport;
}

struct WorkerOptions {
  WorkerOptions();  // Builtin data preset; explicit values remain usable.
  static Result<WorkerOptions> from_profile(const RuntimeProfile& profile);
  int drain_batch = 0;
  std::chrono::milliseconds idle_poll{0};
  double llm_rate_limit = 0.0;  // zero disables throttling
  std::chrono::milliseconds startup_delay{0};
  std::int64_t batch_threshold = 0;
  int batch_max_messages = 0;
  std::string batch_endpoint;
};

struct WorkerStatus {
  std::int64_t pending = 0;
  std::optional<std::int64_t> failed;
  std::optional<std::int64_t> executing;
  // Subset of `executing`: claims whose owner is gone (restart / dead process)
  // or older than the preset lease. Unknown (null) when counts are unknown;
  // unknown is never reported as zero. They are not retried unless requeued
  // explicitly or retry_mode is "automatic".
  std::optional<std::int64_t> stalled;
  // Pending rows this process cannot claim (e.g. unreadable attempt history).
  // They are skipped so the rest of the queue keeps draining.
  std::int64_t quarantined = 0;
  std::string retry_mode = "explicit_requeue";
  bool counts_known = false;
  std::int64_t processed = 0;
  std::int64_t errors = 0;
  std::string mode = "idle";
  double rate = 0.0;
  std::optional<std::string> batch_id;
  std::int64_t batch_submitted = 0;
  bool running = false;
  bool paused = false;
  // Present only while the automatic batch submission awaits the owner's
  // usage confirmation (mode "awaiting_usage_confirmation"); messages stay pending.
  std::optional<Json> usage_pending;
  Json to_json() const;  // Python keys; "rate" formatted "%.1f/s"; + "running","paused"
};

class SemanticWorker {
 public:
  // Direct clients retain their supplied default analyzer. Runtime binds the
  // current resolved profile explicitly; each drain uses one profile snapshot.
  SemanticWorker(Database& db, SemanticLLM& llm, GraphEngine& graph, const Config& cfg, const Secrets& secrets,
                 EventBus& bus, net::HttpTransport& http, const SemanticAnalyzer& regex, TaskEngine* tasks = nullptr,
                 std::optional<WorkerOptions> opts = {}, const Json& profile_overrides = Json::object(),
                 AnalyzerBinding analyzer_binding = AnalyzerBinding::ConstructorDefault);
  ~SemanticWorker();  // stop()
  SemanticWorker(const SemanticWorker&) = delete;
  SemanticWorker& operator=(const SemanticWorker&) = delete;

  void start();   // idempotent
  void stop();    // joins
  void wake();
  // Answers a batch submission paused by the shared usage guard. Approval is
  // executed by the next drain; rejection records the denial and pauses the
  // worker (no retry loop). Messages are never altered.
  Result<Json> confirm_usage(bool approved, std::string_view receipt_id, std::string_view ref);
  void pause();
  void resume();
  WorkerStatus status() const;

  // Bulk requeue (one operation, history of earlier attempts preserved).
  // filter (all optional): failed (bool, default true), stalled (bool, default
  // true), conv_id, since (ISO time of the last attempt), error_class (error
  // code of the last attempt), ids (array), dry_run (bool). Returns the
  // matched/requeued counts (known numbers only) and the affected ids.
  Result<Json> requeue(const Json& filter = Json::object());
  // "explicit_requeue" | "automatic" (profile retry_mode is the initial preset).
  Status set_retry_mode(std::string_view mode);

  // One drain pass in the calling thread; returns processed count.
  Result<int> drain_once();
  Result<Json> runtime_profile() const;

 private:
  void run();
  // retry_mode=automatic: requeue due failed/stalled items through the usage guard.
  void auto_requeue(bool use_llm);

  Database& db_;
  SemanticLLM& llm_;
  GraphEngine& graph_;
  const Config& cfg_;
  const Secrets& secrets_;
  EventBus& bus_;
  net::HttpTransport& http_;
  const SemanticAnalyzer& regex_;
  AnalyzerBinding analyzer_binding_;
  TaskEngine* tasks_;
  Result<RuntimeProfile> profile_;
  WorkerOptions opts_;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::thread thread_;
  bool stop_ = false;
  bool wake_ = false;
  std::atomic<bool> paused_{false};
  std::atomic<std::int64_t> processed_{0};
  std::atomic<std::int64_t> errors_{0};
  double rate_ = 0.0;
  std::string mode_ = "idle";
  std::optional<std::string> batch_id_;
  std::int64_t batch_submitted_ = 0;
  std::optional<Json> usage_pending_;       // guarded by mu_
  std::optional<Json> usage_confirmation_;  // guarded by mu_
  std::optional<Json> retry_pending_;        // automatic resend awaiting the owner; guarded by mu_
  std::optional<Json> retry_confirmation_;   // guarded by mu_
  std::string retry_mode_;                   // guarded by mu_
  std::string instance_;                     // claim-owner identity of this worker
  std::set<std::string> quarantine_;         // guarded by mu_
  ScopedSubscription import_sub_;
};

}  // namespace loom
