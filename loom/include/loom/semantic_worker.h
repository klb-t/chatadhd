// loom/semantic_worker.h — port of engine/semantic_worker.py. [OWNER: wave 2 net/chat/worker]
//
// Background thread draining messages with semantic_status='pending'.
// Behaviour to preserve (defaults in WorkerOptions mirror the Python constants):
//   loop (after startup_delay): paused -> wait on wake (5 s) ; else
//     drain_once(); when it processed 0: poll an active batch, mode "idle",
//     wait on wake up to idle_poll; exceptions logged, 5 s back-off.
//   drain_once(): msgs = get_unanalysed_msgs(drain_batch); use_llm =
//     llm.enabled() && config.semantic_analysis; if count_pending > 500 &&
//     use_llm && secrets.anthropic_batch_key -> submit a Message Batch (one
//     active at a time, up to 10000 msgs, model = semantic_model without the
//     "provider/" prefix, POST https://api.anthropic.com/v1/messages/batches
//     with x-api-key/anthropic-version 2023-06-01, timeout 120) and return 0;
//     else per message (stop on stop/pause): analysis = llm.analyse (then
//     sleep 1/llm_rate_limit) or the regex unified dict ->
//     graph.ingest_analysis(id, conv_id, analysis) -> mark_analysed; errors
//     -> ++errors and mark_analysed({"source":"error"}). Afterwards rate =
//     count/elapsed, and when count > 0 emit semantic:progress {"processed":
//     count, "pending": remaining, "mode"}.
//   batch poll: GET .../batches/{id}; "ended" -> fetch JSONL results
//     (succeeded -> concat text blocks, strip fences, JSON, source
//     "llm_batch", ingest + mark; else mark {"source":"batch_error"}), emit
//     semantic:progress {"processed","pending","mode":"batch"}; failed/
//     canceled/expired -> forget batch.
//   status(): {"pending","processed","errors","mode","rate":"<x.y>/s",
//     "batch_id","batch_submitted"}; modes: idle | paused | regex | llm |
//     batch_submit | batch_wait | batch_poll | batch_ingest.
//   Wakes on import:done.
// Loom additions: the active batch id is persisted (loom_tasks, kind
// "semantic.anthropic_batch") so a restart resumes polling instead of
// re-submitting; stop() is prompt (condition variable, no sleeps > 100 ms
// between stop checks); drain_once() is public for deterministic tests.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
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
  std::int64_t processed = 0;
  std::int64_t errors = 0;
  std::string mode = "idle";
  double rate = 0.0;
  std::optional<std::string> batch_id;
  std::int64_t batch_submitted = 0;
  bool running = false;
  bool paused = false;
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
  void pause();
  void resume();
  WorkerStatus status() const;

  // One drain pass in the calling thread; returns processed count.
  Result<int> drain_once();
  Result<Json> runtime_profile() const;

 private:
  void run();

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
  ScopedSubscription import_sub_;
};

}  // namespace loom
