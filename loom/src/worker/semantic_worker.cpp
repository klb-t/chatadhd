// OWNER: wave 2 net/chat/worker. Control surface is real (flags, status);
// the analysis loop is a stub, so no thread is started yet.
#include "loom/semantic_worker.h"

#include <cstdio>

#include "loom/db.h"
#include "stub.h"

namespace loom {

Json WorkerStatus::to_json() const {
  char rate_buf[32];
  std::snprintf(rate_buf, sizeof rate_buf, "%.1f/s", rate);
  return Json{{"pending", pending},
              {"processed", processed},
              {"errors", errors},
              {"mode", mode},
              {"rate", rate_buf},
              {"batch_id", batch_id ? Json(*batch_id) : Json(nullptr)},
              {"batch_submitted", batch_submitted},
              {"running", running},
              {"paused", paused}};
}

SemanticWorker::SemanticWorker(Database& db, SemanticLLM& llm, GraphEngine& graph, const Config& cfg,
                               const Secrets& secrets, EventBus& bus, net::HttpTransport& http,
                               const SemanticAnalyzer& regex, TaskEngine* tasks, WorkerOptions opts)
    : db_(db),
      llm_(llm),
      graph_(graph),
      cfg_(cfg),
      secrets_(secrets),
      bus_(bus),
      http_(http),
      regex_(regex),
      tasks_(tasks),
      opts_(std::move(opts)) {
  import_sub_ = ScopedSubscription(bus_, bus_.on(events::kImportDone, [this](std::string_view, const Json&) { wake(); }));
}

SemanticWorker::~SemanticWorker() { stop(); }

void SemanticWorker::start() {
  // STUB: wave2 - spawn run() on thread_.
}

void SemanticWorker::stop() {
  {
    std::lock_guard lk(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void SemanticWorker::wake() {
  {
    std::lock_guard lk(mu_);
    wake_ = true;
  }
  cv_.notify_all();
}

void SemanticWorker::pause() {
  paused_.store(true);
  std::lock_guard lk(mu_);
  mode_ = "paused";
}

void SemanticWorker::resume() {
  paused_.store(false);
  wake();
}

WorkerStatus SemanticWorker::status() const {
  WorkerStatus s;
  auto pending = db_.count_pending_semantic();
  if (pending) s.pending = *pending;
  s.processed = processed_.load();
  s.errors = errors_.load();
  s.paused = paused_.load();
  std::lock_guard lk(mu_);
  s.mode = mode_;
  s.rate = rate_;
  s.batch_id = batch_id_;
  s.batch_submitted = batch_submitted_;
  s.running = thread_.joinable();
  return s;
}

Result<int> SemanticWorker::drain_once() {
  return LOOM_NOT_IMPLEMENTED("SemanticWorker::drain_once");  // STUB: wave2
}

void SemanticWorker::run() {}  // STUB: wave2

}  // namespace loom
