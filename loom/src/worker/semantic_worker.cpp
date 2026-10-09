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

Json regex_unified(const SemanticAnalyzer& regex, std::string_view text) { return regex.to_unified_profile(regex.analyse(text)); }

net::Headers batch_headers(const RuntimeProfile& profile, const Secrets& secrets) {
  net::Headers out{{"x-api-key", secrets.get_string(profile.values().at("batch_secret_key").get<std::string>())}};
  const auto& headers = profile.values().at("headers");
  for (auto it = headers.begin(); it != headers.end(); ++it) out.push_back({it.key(), it.value().get<std::string>()});
  return out;
}

void assign_worker_options(WorkerOptions& opts, const RuntimeProfile& profile) {
  const auto& v = profile.values();
  opts.drain_batch = v.at("drain_batch").get<int>();
  opts.idle_poll = std::chrono::milliseconds(v.at("idle_poll_ms").get<std::int64_t>());
  opts.llm_rate_limit = v.at("llm_rate_limit").get<double>();
  opts.startup_delay = std::chrono::milliseconds(v.at("startup_delay_ms").get<std::int64_t>());
  opts.batch_threshold = v.at("batch_threshold").get<std::int64_t>();
  opts.batch_max_messages = v.at("batch_max_messages").get<int>();
  opts.batch_endpoint = v.at("batch_endpoint").get<std::string>();
}

// ── Anthropic Message Batches (submit / poll / fetch) ───────────────────
// Free helpers so no additional private members/methods are needed on
// SemanticWorker; called from member functions with private state passed by
// reference (allowed: the naming happens inside SemanticWorker's own scope).

void fetch_batch_results(Database& db, const Secrets& secrets, net::HttpTransport& http, EventBus& bus,
                         GraphEngine& graph, std::mutex& mu, std::string& mode, const std::string& batch_id,
                         std::atomic<std::int64_t>& processed, const RuntimeProfile& profile) {
  {
    std::lock_guard lk(mu);
    mode = "batch_ingest";
  }
  net::HttpRequest req;
  req.method = "GET";
  req.url = profile.values().at("batch_endpoint").get<std::string>() + "/" + batch_id + "/results";
  req.headers = batch_headers(profile, secrets);
  req.timeout_ms = profile.values().at("results_timeout_ms").get<int>();
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
                        std::int64_t& batch_submitted, std::atomic<std::int64_t>& processed, const RuntimeProfile& profile) {
  std::string bid;
  {
    std::lock_guard lk(mu);
    if (!batch_id) return;
    bid = *batch_id;
    mode = "batch_poll";
  }

  net::HttpRequest req;
  req.method = "GET";
  req.url = profile.values().at("batch_endpoint").get<std::string>() + "/" + bid;
  req.headers = batch_headers(profile, secrets);
  req.timeout_ms = profile.values().at("poll_timeout_ms").get<int>();
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
    fetch_batch_results(db, secrets, http, bus, graph, mu, mode, bid, processed, profile);
    std::lock_guard lk(mu);
    batch_id.reset();
    batch_submitted = 0;
  } else if (std::find(profile.values().at("terminal_statuses").begin(), profile.values().at("terminal_statuses").end(), status) != profile.values().at("terminal_statuses").end()) {
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
                     int batch_max_messages, const std::string& batch_endpoint, const RuntimeProfile& profile) {
  {
    std::lock_guard lk(mu);
    if (batch_id) return 0;
    mode = "batch_submit";
  }
  auto msgsr = db.get_unanalysed_msgs(batch_max_messages);
  if (!msgsr || msgsr->empty()) return 0;

  std::string model = cfg_string(cfg, "semantic_model");
  if (profile.values().at("strip_model_provider").get<bool>())
    if (auto p = model.find('/'); p != std::string::npos) model = model.substr(p + 1);

  Json batch_requests = Json::array();
  for (const auto& m : *msgsr) {
    batch_requests.push_back(
        Json{{"custom_id", m.id},
            {"params", Json{{"model", model},
                           {"max_tokens", profile.values().at("batch_output_tokens")},
                           {"messages", Json::array({Json{{"role", "user"},
                             {"content", std::string(kAnalysisPrompt) + std::string(utf8::prefix(m.text, profile.values().at("batch_input_chars").get<std::size_t>()))}}})}}}});
  }

  net::HttpRequest req;
  req.method = "POST";
  req.url = batch_endpoint;
  req.headers = batch_headers(profile, secrets);
  req.headers.push_back({"content-type", "application/json"});
  req.body = json::dump(Json{{"requests", batch_requests}});
  req.timeout_ms = profile.values().at("submit_timeout_ms").get<int>();

  auto resp = http.send(req);
  if (!resp) {
    log::error("loom.semantic_worker", "Batch submit error: {}", resp.error().message);
    return 0;
  }
  if (resp->status == 200 || resp->status == 201) {
    auto j = resp->json();
    std::string id = j ? json::get_string(*j, "id") : std::string();
    std::int64_t submitted = static_cast<std::int64_t>(batch_requests.size());
    double cost = static_cast<double>(submitted) * profile.values().at("batch_estimate_per_message").get<double>();
    {
      std::lock_guard lk(mu);
      batch_id = id;
      batch_submitted = submitted;
      mode = "batch_wait";
    }
    log::info("loom.semantic_worker", "Batch submitted: {} ({} msgs, ~${:.2f})", id, submitted, cost);
    return 0;
  }
  log::warn("loom.semantic_worker", "Batch submit failed {}: {}", resp->status, std::string(utf8::prefix(resp->body, profile.values().at("submit_error_body_chars").get<std::size_t>())));
  return 0;
}

}  // namespace

WorkerOptions::WorkerOptions() {
  if (auto profile = RuntimeProfile::builtin("worker")) assign_worker_options(*this, *profile);
}

Result<WorkerOptions> WorkerOptions::from_profile(const RuntimeProfile& profile) {
  if (profile.domain() != "worker") return Error(Errc::InvalidArgument, "expected worker runtime profile");
  LOOM_TRY_ASSIGN(auto builtin, RuntimeProfile::builtin("worker"));
  LOOM_TRY_ASSIGN(auto checked, builtin.with_values(profile.values()));
  WorkerOptions out;
  assign_worker_options(out, checked);
  return out;
}

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
                               const SemanticAnalyzer& regex, TaskEngine* tasks, std::optional<WorkerOptions> opts,
                               const Json& profile_overrides, AnalyzerBinding analyzer_binding)
    : db_(db),
      llm_(llm),
      graph_(graph),
      cfg_(cfg),
      secrets_(secrets),
      bus_(bus),
      http_(http),
      regex_(regex),
      analyzer_binding_(analyzer_binding),
      tasks_(tasks),
      profile_(RuntimeProfile::load("worker", cfg.path().parent_path(), profile_overrides)),
      opts_(opts ? std::move(*opts) : WorkerOptions()) {
  if (opts && profile_) {
    profile_ = profile_->with_overrides(Json{{"drain_batch", opts_.drain_batch},
      {"idle_poll_ms", opts_.idle_poll.count()}, {"llm_rate_limit", opts_.llm_rate_limit},
      {"startup_delay_ms", opts_.startup_delay.count()}, {"batch_threshold", opts_.batch_threshold},
      {"batch_max_messages", opts_.batch_max_messages}, {"batch_endpoint", opts_.batch_endpoint}});
  }
  if (!opts && profile_) assign_worker_options(opts_, *profile_);
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

Result<Json> SemanticWorker::runtime_profile() const { LOOM_TRY(profile_); return profile_->inspection(); }

Result<int> SemanticWorker::drain_once() {
  LOOM_TRY(profile_);
  auto msgsr = db_.get_unanalysed_msgs(opts_.drain_batch);
  if (!msgsr) return msgsr.error();
  auto& msgs = *msgsr;
  if (msgs.empty()) return 0;

  // Resolve once before any transport or write. A malformed overlay is an
  // operation error, never a reason to silently use the built-in analyzer.
  LOOM_TRY_ASSIGN(auto analyzer_profile, RuntimeProfile::load("semantic_analyzer", db_.path().parent_path()));
  // Builtin equality describes the current values, not the analyzer retained
  // by the constructor (which may have been built from a removed overlay).
  std::unique_ptr<SemanticAnalyzer> effective_analyzer;
  if (analyzer_binding_ == AnalyzerBinding::RuntimeProfile || !analyzer_profile.is_builtin()) {
    LOOM_TRY_ASSIGN(effective_analyzer, SemanticAnalyzer::create_with_profile(analyzer_profile));
  }
  const auto& analyzer = effective_analyzer ? *effective_analyzer : regex_;

  bool use_llm = llm_.enabled() && cfg_bool(cfg_, "semantic_analysis", true);

  auto pending_r = db_.count_pending_semantic();
  std::int64_t pending_total = pending_r.value_or(0);
  bool has_batch_key = !secrets_.get_string(profile_->values().at("batch_secret_key").get<std::string>()).empty();

  if (pending_total > opts_.batch_threshold && use_llm && has_batch_key) {
    return submit_batch_api(db_, cfg_, secrets_, http_, mu_, mode_, batch_id_, batch_submitted_,
                            opts_.batch_max_messages, opts_.batch_endpoint, *profile_);
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
      Json analysis = use_llm ? llm_.analyse(m.text, analyzer) : regex_unified(analyzer, m.text);
      if (!analyzer_profile.is_builtin()) analysis["analyzer_profile_hash"] = analyzer_profile.hash();
      if (use_llm && opts_.llm_rate_limit > 0) {
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
  const double denominator = std::max(elapsed, profile_->values().at("rate_elapsed_floor").get<double>());
  double rate = denominator > 0 ? count / denominator : 0;
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
  if (!profile_) { log::error("loom.semantic_worker", "{}", profile_.error().message); return; }
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
      cv_.wait_for(lk, std::chrono::milliseconds(profile_->values().at("pause_poll_ms").get<std::int64_t>()), [this] { return stop_ || wake_; });
      wake_ = false;
      if (stop_) return;
      continue;
    }
    try {
      auto processed = drain_once();
      if (!processed) {
        log::error("loom.semantic_worker", "SemanticWorker loop error: {}", processed.error().message);
        std::unique_lock lk(mu_);
        cv_.wait_for(lk, std::chrono::milliseconds(profile_->values().at("error_backoff_ms").get<std::int64_t>()), [this] { return stop_; });
        if (stop_) return;
        continue;
      }
      if (*processed == 0) {
        check_batch_status(db_, secrets_, http_, bus_, graph_, mu_, mode_, batch_id_, batch_submitted_, processed_, *profile_);
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
      cv_.wait_for(lk, std::chrono::milliseconds(profile_->values().at("error_backoff_ms").get<std::int64_t>()), [this] { return stop_; });
      if (stop_) return;
    }
  }
}

}  // namespace loom
