// OWNER: wave 2 net/chat/worker. Port of engine/semantic_worker.py.
#include "loom/semantic_worker.h"

#include <algorithm>
#include <cstdio>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/graph_engine.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"
#include "stub.h"

namespace loom {

namespace {

std::string cfg_string(const Config& cfg, std::string_view key, std::string fallback = "") {
  Json v = cfg.get(key);
  return v.is_string() ? v.get<std::string>() : std::move(fallback);
}

bool cfg_bool(const Config& cfg, std::string_view key, bool fallback) {
  return json::truthy(cfg.get(key, fallback));
}

Json regex_unified(const SemanticAnalyzer& regex, std::string_view text) { return SemanticAnalyzer::to_unified(regex.analyse(text)); }

// ── Anthropic Message Batches (submit / poll / fetch) ───────────────────
// Free helpers so no additional private members/methods are needed on
// SemanticWorker; called from member functions with private state passed by
// reference (allowed: the naming happens inside SemanticWorker's own scope).

void fetch_batch_results(Database& db, const Secrets& secrets, net::HttpTransport& http, EventBus& bus,
                         GraphEngine& graph, std::mutex& mu, std::string& mode, const std::string& batch_id,
                         std::atomic<std::int64_t>& processed) {
  {
    std::lock_guard lk(mu);
    mode = "batch_ingest";
  }
  net::HttpRequest req;
  req.method = "GET";
  req.url = "https://api.anthropic.com/v1/messages/batches/" + batch_id + "/results";
  req.headers = {{"x-api-key", secrets.get_string("anthropic_batch_key")}, {"anthropic-version", "2023-06-01"}};
  req.timeout_ms = 300000;
  req.stream = true;

  int count = 0, errors = 0;
  std::string buf;
  auto handle_line = [&](std::string_view line) {
    if (line.empty()) return;
    auto j = json::parse(line);
    if (!j) {
      ++errors;
      return;
    }
    std::string msg_id = json::get_string(*j, "custom_id");
    const Json* result = json::find(*j, "result");
    if (result && json::get_string(*result, "type") == "succeeded") {
      try {
        std::string raw;
        const Json& content = result->at("message").at("content");
        for (const auto& block : content) {
          if (json::get_string(block, "type") == "text") raw += json::get_string(block, "text");
        }
        auto parsed = SemanticLLM::parse_response_json(raw);
        if (!parsed) {
          ++errors;
          return;
        }
        Json analysis = *parsed;
        analysis["source"] = "llm_batch";
        auto mr = db.get_msg(msg_id);
        std::string conv_id = (mr && *mr) ? (*mr)->conv_id : std::string();
        graph.ingest_analysis(msg_id, conv_id, analysis);
        (void)db.mark_analysed(msg_id, analysis);
        ++count;
        processed.fetch_add(1);
      } catch (const std::exception&) {
        ++errors;
      }
    } else {
      (void)db.mark_analysed(msg_id, Json{{"source", "batch_error"}});
      ++errors;
    }
  };

  net::StreamSink sink;
  sink.on_data = [&](std::string_view chunk) -> bool {
    buf.append(chunk);
    std::size_t pos;
    while ((pos = buf.find('\n')) != std::string::npos) {
      handle_line(std::string_view(buf).substr(0, pos));
      buf.erase(0, pos + 1);
    }
    return true;
  };
  auto resp = http.send(req, &sink);
  if (!buf.empty()) handle_line(buf);
  if (!resp || resp->status != 200) {
    log::warn("loom.semantic_worker", "Batch results failed: {}", resp ? resp->status : -1);
    return;
  }
  log::info("loom.semantic_worker", "Batch ingested: {} ok, {} errors", count, errors);
  auto pending_r = db.count_pending_semantic();
  bus.emit(events::kSemanticProgress, Json{{"processed", count}, {"pending", pending_r.value_or(0)}, {"mode", "batch"}});
}

void check_batch_status(Database& db, const Secrets& secrets, net::HttpTransport& http, EventBus& bus, GraphEngine& graph,
                        std::mutex& mu, std::string& mode, std::optional<std::string>& batch_id,
                        std::int64_t& batch_submitted, std::atomic<std::int64_t>& processed) {
  std::string bid;
  {
    std::lock_guard lk(mu);
    if (!batch_id) return;
    bid = *batch_id;
    mode = "batch_poll";
  }

  net::HttpRequest req;
  req.method = "GET";
  req.url = "https://api.anthropic.com/v1/messages/batches/" + bid;
  req.headers = {{"x-api-key", secrets.get_string("anthropic_batch_key")}, {"anthropic-version", "2023-06-01"}};
  req.timeout_ms = 30000;
  auto resp = http.send(req);
  if (!resp || resp->status != 200) {
    log::warn("loom.semantic_worker", "Batch poll failed: {}", resp ? resp->status : -1);
    return;
  }
  auto j = resp->json();
  if (!j) return;
  std::string status = json::get_string(*j, "processing_status");
  const Json* counts = json::find(*j, "request_counts");
  log::info("loom.semantic_worker", "Batch {}: {} (ok={}, err={})", bid, status,
           counts ? json::get_int(*counts, "succeeded", 0) : 0, counts ? json::get_int(*counts, "errored", 0) : 0);

  if (status == "ended") {
    fetch_batch_results(db, secrets, http, bus, graph, mu, mode, bid, processed);
    std::lock_guard lk(mu);
    batch_id.reset();
    batch_submitted = 0;
  } else if (status == "failed" || status == "canceled" || status == "expired") {
    log::warn("loom.semantic_worker", "Batch {}: {}", bid, status);
    std::lock_guard lk(mu);
    batch_id.reset();
    batch_submitted = 0;
  }
}

// Returns 0 always (matches Python _submit_batch_api, which never returns a
// processed count on the submit path).
int submit_batch_api(Database& db, const Config& cfg, const Secrets& secrets, net::HttpTransport& http, std::mutex& mu,
                     std::string& mode, std::optional<std::string>& batch_id, std::int64_t& batch_submitted,
                     int batch_max_messages, const std::string& batch_endpoint) {
  {
    std::lock_guard lk(mu);
    if (batch_id) return 0;
    mode = "batch_submit";
  }
  auto msgsr = db.get_unanalysed_msgs(batch_max_messages);
  if (!msgsr || msgsr->empty()) return 0;

  std::string model = cfg_string(cfg, "semantic_model");
  if (auto p = model.find('/'); p != std::string::npos) model = model.substr(p + 1);

  Json batch_requests = Json::array();
  for (const auto& m : *msgsr) {
    batch_requests.push_back(
        Json{{"custom_id", m.id},
            {"params", Json{{"model", model},
                           {"max_tokens", 800},
                           {"messages", Json::array({Json{{"role", "user"},
                             {"content", std::string(kAnalysisPrompt) + std::string(utf8::prefix(m.text, 3000))}}})}}}});
  }

  net::HttpRequest req;
  req.method = "POST";
  req.url = batch_endpoint;
  req.headers = {{"x-api-key", secrets.get_string("anthropic_batch_key")},
                {"anthropic-version", "2023-06-01"},
                {"content-type", "application/json"}};
  req.body = json::dump(Json{{"requests", batch_requests}});
  req.timeout_ms = 120000;

  auto resp = http.send(req);
  if (!resp) {
    log::error("loom.semantic_worker", "Batch submit error: {}", resp.error().message);
    return 0;
  }
  if (resp->status == 200 || resp->status == 201) {
    auto j = resp->json();
    std::string id = j ? json::get_string(*j, "id") : std::string();
    std::int64_t submitted = static_cast<std::int64_t>(batch_requests.size());
    double cost = static_cast<double>(submitted) * 0.000065;
    {
      std::lock_guard lk(mu);
      batch_id = id;
      batch_submitted = submitted;
      mode = "batch_wait";
    }
    log::info("loom.semantic_worker", "Batch submitted: {} ({} msgs, ~${:.2f})", id, submitted, cost);
    return 0;
  }
  log::warn("loom.semantic_worker", "Batch submit failed {}: {}", resp->status, std::string(utf8::prefix(resp->body, 200)));
  return 0;
}

}  // namespace

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
  std::lock_guard lk(mu_);
  if (thread_.joinable()) return;  // idempotent
  stop_ = false;
  thread_ = std::thread([this] { run(); });
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
  if (auto pending = db_.count_pending_semantic()) s.pending = *pending;
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
  auto msgsr = db_.get_unanalysed_msgs(opts_.drain_batch);
  if (!msgsr) return msgsr.error();
  auto& msgs = *msgsr;
  if (msgs.empty()) return 0;

  bool use_llm = llm_.enabled() && cfg_bool(cfg_, "semantic_analysis", true);

  auto pending_r = db_.count_pending_semantic();
  std::int64_t pending_total = pending_r.value_or(0);
  bool has_batch_key = !secrets_.get_string("anthropic_batch_key").empty();

  if (pending_total > opts_.batch_threshold && use_llm && has_batch_key) {
    return submit_batch_api(db_, cfg_, secrets_, http_, mu_, mode_, batch_id_, batch_submitted_,
                            opts_.batch_max_messages, opts_.batch_endpoint);
  }

  {
    std::lock_guard lk(mu_);
    mode_ = use_llm ? "llm" : "regex";
  }

  double t0 = timeutil::monotonic_seconds();
  int count = 0;

  for (const auto& m : msgs) {
    {
      std::lock_guard lk(mu_);
      if (stop_ || paused_.load()) break;
    }
    try {
      Json analysis = use_llm ? llm_.analyse(m.text) : regex_unified(regex_, m.text);
      if (use_llm) {
        // Interruptible rate-limit wait: no blind sleep, so stop() is prompt.
        std::unique_lock lk(mu_);
        auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(1.0 / opts_.llm_rate_limit));
        cv_.wait_for(lk, wait, [this] { return stop_; });
        if (stop_) break;
      }
      graph_.ingest_analysis(m.id, m.conv_id, analysis);
      (void)db_.mark_analysed(m.id, analysis);
      ++count;
      processed_.fetch_add(1);
    } catch (const std::exception& e) {
      log::debug("loom.semantic_worker", "Worker fail on {}: {}", m.id, e.what());
      errors_.fetch_add(1);
      (void)db_.mark_analysed(m.id, Json{{"source", "error"}});
    }
  }

  double elapsed = timeutil::monotonic_seconds() - t0;
  double rate = count / std::max(elapsed, 0.01);
  std::string mode_copy;
  {
    std::lock_guard lk(mu_);
    rate_ = rate;
    mode_copy = mode_;
  }
  if (count > 0) {
    auto remaining_r = db_.count_pending_semantic();
    std::int64_t remaining = remaining_r.value_or(0);
    log::info("loom.semantic_worker", "Semantic: {} done ({}, {:.1f}/s, {} left)", count, mode_copy, rate, remaining);
    bus_.emit(events::kSemanticProgress, Json{{"processed", count}, {"pending", remaining}, {"mode", mode_copy}});
  }
  return count;
}

void SemanticWorker::run() {
  {
    std::unique_lock lk(mu_);
    cv_.wait_for(lk, opts_.startup_delay, [this] { return stop_; });
    if (stop_) return;
  }

  while (true) {
    {
      std::lock_guard lk(mu_);
      if (stop_) return;
    }
    if (paused_.load()) {
      std::unique_lock lk(mu_);
      cv_.wait_for(lk, std::chrono::seconds(5), [this] { return stop_ || wake_; });
      wake_ = false;
      if (stop_) return;
      continue;
    }
    try {
      auto processed = drain_once();
      if (!processed) {
        log::error("loom.semantic_worker", "SemanticWorker loop error: {}", processed.error().message);
        std::unique_lock lk(mu_);
        cv_.wait_for(lk, std::chrono::seconds(5), [this] { return stop_; });
        if (stop_) return;
        continue;
      }
      if (*processed == 0) {
        check_batch_status(db_, secrets_, http_, bus_, graph_, mu_, mode_, batch_id_, batch_submitted_, processed_);
        {
          std::lock_guard lk(mu_);
          mode_ = "idle";
        }
        std::unique_lock lk(mu_);
        cv_.wait_for(lk, opts_.idle_poll, [this] { return stop_ || wake_; });
        wake_ = false;
        if (stop_) return;
      }
    } catch (const std::exception& e) {
      log::error("loom.semantic_worker", "SemanticWorker loop error: {}", e.what());
      std::unique_lock lk(mu_);
      cv_.wait_for(lk, std::chrono::seconds(5), [this] { return stop_; });
      if (stop_) return;
    }
  }
}

}  // namespace loom
