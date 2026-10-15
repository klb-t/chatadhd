// OWNER: wave 2 net/chat/worker. Port of engine/semantic_worker.py.
#include "loom/annotation_layer.h"
#include "loom/semantic_worker.h"
#include "loom/usage_spend.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <set>

#include "loom/analysis_mode.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/graph_engine.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/usage_policy.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/use_model.h"
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


// Message Batches route from the worker profile: the profile names the
// address, the secret and the extra headers; the provider manifest that offers
// the batch capability at that host supplies the adapter and auth scheme.
llm::RouteSelector batch_selector(const llm::Services& services, const RuntimeProfile& profile) {
  llm::RouteSelector selector;
  selector.endpoint = profile.values().at("batch_endpoint").get<std::string>();
  selector.provider = llm::provider_for_address(services, "batch", "messages.batches", selector.endpoint, true);
  selector.secret = profile.values().at("batch_secret_key").get<std::string>();
  net::Headers headers;
  const auto& configured = profile.values().at("headers");
  for (auto it = configured.begin(); it != configured.end(); ++it) headers.push_back({it.key(), it.value().get<std::string>()});
  selector.headers = std::move(headers);
  selector.content_type_header = "content-type";
  return selector;
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

// ── Queue inspection: failed / stalled items ───────────────────────────
struct QueueItem {
  std::string id, conv_id, kind, when, error_class, reason;  // kind: "failed" | "stalled"
  bool declined = false;  // last attempt carries usage_declined: no automatic resend
};

struct QueueFilter {
  bool failed = true, stalled = true, active_only = true;
  std::string conv_id, error_class;
  std::optional<timeutil::Clock::time_point> since;
  std::set<std::string> ids;
  bool dry_run = false;
};

Result<QueueFilter> parse_queue_filter(const Json& j) {
  QueueFilter f;
  if (j.is_null()) return f;
  if (!j.is_object()) return Error(Errc::InvalidArgument, "requeue filter must be an object");
  for (auto it = j.begin(); it != j.end(); ++it) {
    const auto& k = it.key();
    const auto& v = it.value();
    auto bad = [&] { return Error(Errc::InvalidArgument, "invalid requeue filter field: " + k); };
    if (k == "failed" || k == "stalled" || k == "dry_run") {
      if (!v.is_boolean()) return bad();
      (k == "failed" ? f.failed : k == "stalled" ? f.stalled : f.dry_run) = v.get<bool>();
    } else if (k == "conv_id" || k == "error_class") {
      if (!v.is_string()) return bad();
      (k == "conv_id" ? f.conv_id : f.error_class) = v.get<std::string>();
    } else if (k == "since") {
      if (!v.is_string()) return bad();
      f.since = timeutil::parse_iso_utc(v.get<std::string>());
      if (!f.since) return bad();
    } else if (k == "ids") {
      if (!v.is_array()) return bad();
      for (const auto& e : v) {
        if (!e.is_string()) return bad();
        f.ids.insert(e.get<std::string>());
      }
    } else {
      return Error(Errc::InvalidArgument, "unknown requeue filter field: " + k);
    }
  }
  return f;
}

const Json* last_attempt(const Json& metadata) {
  const Json* attempts = json::find(metadata, "loom_semantic_attempts");
  if (!attempts || !attempts->is_array() || attempts->empty() || !attempts->back().is_object()) return nullptr;
  return &attempts->back();
}

// nullopt: the executing claim is live (or not old enough to doubt).
std::optional<std::string> stall_reason(const Json& metadata, std::int64_t lease_ms) {
  const Json* rec = last_attempt(metadata);
  if (!rec || json::get_string(*rec, "state") != "executing") return "no_claim_record";
  const auto owner = rec->contains("owner") ? attempt_owner_state((*rec)["owner"]) : OwnerState::Unknown;
  if (owner == OwnerState::Gone) return "owner_gone";
  if (owner == OwnerState::Live) return std::nullopt;
  auto started = timeutil::parse_iso_utc(json::get_string(*rec, "started"));
  if (!started) return "no_claim_time";
  if (timeutil::Clock::now() - *started >= std::chrono::milliseconds(lease_ms)) return "lease_expired";
  return std::nullopt;
}

Result<std::vector<QueueItem>> scan_queue(Database& db, std::int64_t lease_ms, const QueueFilter& f) {
  auto lock = db.lock();
  std::string sql = "SELECT id, conv_id, semantic_status, metadata FROM messages "
                    "WHERE semantic_status IN ('failed','executing')";
  if (f.active_only) sql += " AND status = 'active'";
  if (!f.conv_id.empty()) sql += " AND conv_id = ?";
  sql += " ORDER BY created ASC";
  LOOM_TRY_ASSIGN(auto st, db.conn().prepare(sql));
  if (!f.conv_id.empty()) st.bind(1, f.conv_id);
  std::vector<QueueItem> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    QueueItem item;
    item.id = st.get_text(0);
    item.conv_id = st.get_text(1);
    const auto status = st.get_text(2);
    const auto raw = st.get_opt_text(3);
    Json metadata = Json::object();
    if (raw) { auto parsed = json::parse(*raw); if (parsed) metadata = *parsed; }
    const Json* rec = last_attempt(metadata);
    item.declined = rec && rec->contains("usage_declined");
    if (status == "failed") {
      item.kind = "failed";
      if (rec) {
        item.when = json::get_string(*rec, "finished");
        if (const Json* err = json::find(*rec, "error"); err && err->is_object()) item.error_class = json::get_string(*err, "code");
      }
    } else {
      auto reason = stall_reason(metadata, lease_ms);
      if (!reason) continue;  // live claim
      item.kind = "stalled";
      item.reason = *reason;
      if (rec) item.when = json::get_string(*rec, "started");
    }
    if ((item.kind == "failed" && !f.failed) || (item.kind == "stalled" && !f.stalled)) continue;
    if (!f.ids.empty() && !f.ids.count(item.id)) continue;
    if (!f.error_class.empty() && item.error_class != f.error_class) continue;
    if (f.since) {
      auto when = timeutil::parse_iso_utc(item.when);
      if (!when || *when < *f.since) continue;
    }
    out.push_back(std::move(item));
  }
  return out;
}

// ── Anthropic Message Batches (submit / poll / fetch) ───────────────────
// Free helpers so no additional private members/methods are needed on
// SemanticWorker; called from member functions with private state passed by
// reference (allowed: the naming happens inside SemanticWorker's own scope).

void fetch_batch_results(Database& db, const Config& cfg, const Secrets& secrets, net::HttpTransport& http,
                         EventBus& bus, GraphEngine& graph, std::mutex& mu, std::string& mode, const std::string& batch_id,
                         std::atomic<std::int64_t>& processed, const RuntimeProfile& profile) {
  {
    std::lock_guard lk(mu);
    mode = "batch_ingest";
  }
  const llm::Services services(cfg, secrets, http);
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

  // JSONL: each line is applied as it arrives; nothing is retained.
  llm::ModelRequest request;
  request.kind = "semantic.batch.results";
  request.action = "semantic.batch";
  request.models = {llm::ModelSpec{}};
  request.route = batch_selector(services, profile);
  request.operation = llm::Operation::BatchResults;
  request.target = batch_id;
  request.timeout_ms = profile.values().at("results_timeout_ms").get<int>();
  request.stream = true;
  request.capture = true;
  request.retain_body = false;
  request.usage.guard = false;  // reading results spends nothing
  request.hooks.on_raw = [&](std::string_view chunk) {
    buf.append(chunk);
    std::size_t pos;
    while ((pos = buf.find('\n')) != std::string::npos) {
      handle_line(std::string_view(buf).substr(0, pos));
      buf.erase(0, pos + 1);
    }
  };
  auto outcome = llm::use_model(services, std::move(request));
  if (!buf.empty()) handle_line(buf);
  if (!outcome || outcome->last().state != llm::State::Ok) {
    const bool transport_failed = !outcome || outcome->last().error.has_value();
    log::warn("loom.semantic_worker", "Batch results failed: {}", transport_failed ? -1 : outcome->last().http_status);
    return;
  }
  log::info("loom.semantic_worker", "Batch ingested: {} ok, {} errors", count, errors);
  auto pending_r = db.count_pending_semantic();
  bus.emit(events::kSemanticProgress, Json{{"processed", count}, {"pending", pending_r.value_or(0)}, {"mode", "batch"}});
}

void check_batch_status(Database& db, const Config& cfg, const Secrets& secrets, net::HttpTransport& http,
                        EventBus& bus, GraphEngine& graph, std::mutex& mu, std::string& mode, std::optional<std::string>& batch_id,
                        std::int64_t& batch_submitted, std::atomic<std::int64_t>& processed, const RuntimeProfile& profile) {
  std::string bid;
  {
    std::lock_guard lk(mu);
    if (!batch_id) return;
    bid = *batch_id;
    mode = "batch_poll";
  }

  const llm::Services services(cfg, secrets, http);
  llm::ModelRequest request;
  request.kind = "semantic.batch.status";
  request.action = "semantic.batch";
  request.models = {llm::ModelSpec{}};
  request.route = batch_selector(services, profile);
  request.operation = llm::Operation::BatchStatus;
  request.target = bid;
  request.timeout_ms = profile.values().at("poll_timeout_ms").get<int>();
  request.usage.guard = false;  // polling spends nothing
  auto outcome = llm::use_model(services, std::move(request));
  if (!outcome || outcome->last().state != llm::State::Ok) {
    const bool transport_failed = !outcome || outcome->last().error.has_value();
    log::warn("loom.semantic_worker", "Batch poll failed: {}", transport_failed ? -1 : outcome->last().http_status);
    return;
  }
  auto j = json::parse(outcome->last().body);
  if (!j) return;
  std::string status = json::get_string(*j, "processing_status");
  const Json* counts = json::find(*j, "request_counts");
  log::info("loom.semantic_worker", "Batch {}: {} (ok={}, err={})", bid, status,
           counts ? json::get_int(*counts, "succeeded", 0) : 0, counts ? json::get_int(*counts, "errored", 0) : 0);

  if (status == "ended") {
    fetch_batch_results(db, cfg, secrets, http, bus, graph, mu, mode, bid, processed, profile);
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

// P18: the reason a spending limit gave (limits, reason_codes) on a pending hold or refusal.
void add_limit_reasons(Json& pending, const Json& decision) {
  const Json reasons = usage_limit_reasons(decision);
  for (auto it = reasons.begin(); it != reasons.end(); ++it)
    if (it.key() != "status") pending[it.key()] = it.value();
}

// A refusal by a spending limit, in the shape of a pending hold so that the conversation and the status can say it (status "denied": nothing
// can be answered, `retry_after_s` is when the worker asks the ledger again: the worker profile's limit_refusal_retry_ms later).
Json refusal_pending(const Json& decision, const char* kind, std::size_t messages, const std::string& body_sha, const Json& ids_or_convs,
                     std::int64_t retry_ms) {
  Json out{{"schema", "loom.semantic_usage_pending/1"}, {"kind", kind}, {"status", "denied"}, {"refused", true},
           {"operation_id", decision.value("operation_id", Json(nullptr))}, {"receipt_id", decision.value("receipt_id", Json(nullptr))},
           {"baseline_key", decision.value("baseline_key", Json(nullptr))}, {"projected", decision.value("resources", Json::object())},
           {"messages", messages}, {"body_sha256", body_sha}, {"sent", false},
           {"retry_after_s", static_cast<std::int64_t>(std::time(nullptr)) + (retry_ms + 999) / 1000}};
  out[std::string(kind) == "retry" ? "ids" : "conv_ids"] = ids_or_convs;
  add_limit_reasons(out, decision);
  return out;
}

bool refusal_expired(const std::optional<Json>& slot) {
  return slot && json::get_bool(*slot, "refused", false) && static_cast<std::int64_t>(std::time(nullptr)) >= json::get_int(*slot, "retry_after_s", 0);
}

// Returns 0 always (matches Python _submit_batch_api, which never returns a
// processed count on the submit path).
int submit_batch_api(Database& db, const Config& cfg, const Secrets& secrets, net::HttpTransport& http, std::mutex& mu,
                     std::string& mode, std::optional<std::string>& batch_id, std::int64_t& batch_submitted,
                     int batch_max_messages, const std::string& batch_endpoint, const RuntimeProfile& profile,
                     std::optional<Json>& usage_pending, std::optional<Json>& usage_confirmation) {
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

  // The conversations whose messages the batch holds, the one with the newest message first (the messages come oldest first): the one that shows the
  // question if the guard holds the batch.
  Json conv_ids = Json::array();
  for (auto it = msgsr->rbegin(); it != msgsr->rend(); ++it)
    if (!it->conv_id.empty() && std::find(conv_ids.begin(), conv_ids.end(), Json(it->conv_id)) == conv_ids.end()) conv_ids.push_back(it->conv_id);
  Json batch_requests = Json::array();
  for (const auto& m : *msgsr) {
    batch_requests.push_back(
        Json{{"custom_id", m.id},
            {"params", Json{{"model", model},
                           {"max_tokens", profile.values().at("batch_output_tokens")},
                           {"messages", Json::array({Json{{"role", "user"},
                             {"content", annotations::extend_prompt(kAnalysisPrompt, annotations::segments_plan(cfg), cfg) + std::string(utf8::prefix(m.text, profile.values().at("batch_input_chars").get<std::size_t>()))}}})}}}});
  }

  const llm::Services services(cfg, secrets, http);
  llm::ModelRequest request;
  request.kind = "semantic.batch";
  request.action = "semantic.batch";
  request.models = {llm::ModelSpec{}};
  request.route = batch_selector(services, profile);
  request.operation = llm::Operation::BatchSubmit;
  request.batch_requests = batch_requests;
  request.timeout_ms = profile.values().at("submit_timeout_ms").get<int>();
  auto prepared = llm::prepare_call(services, request);
  if (!prepared) {
    log::error("loom.semantic_worker", "Batch submit error: {}", prepared.error().message);
    return 0;
  }

  // Shared usage guard: admit before transport, settle after. A paused
  // receipt keeps the messages pending until the owner answers.
  const std::string body_sha = Sha256::hex(prepared->request.body);
  const double declared_cost = static_cast<double>(batch_requests.size()) *
                               profile.values().at("batch_estimate_per_message").get<double>();
  std::string operation_id;
  Json confirmation = Json::object();
  {
    std::lock_guard lk(mu);
    if (usage_pending && json::get_string(*usage_pending, "body_sha256") != body_sha) {
      usage_pending.reset();  // the batch changed: an old receipt cannot authorize it
      usage_confirmation.reset();
    }
    if (usage_pending) {
      operation_id = json::get_string(*usage_pending, "operation_id");
      if (usage_confirmation) confirmation = *usage_confirmation;
    }
  }
  Json estimate{{"baseline_key", usage_baseline_key(cfg, "semantic.batch", batch_endpoint, model)},
      {"resources", Json{{"messages", batch_requests.size()}, {"cost_usd", declared_cost}}},
      {"estimate_provenance", Json{{"cost_usd", "declared"}, {"messages", "counted"}, {"input_bytes", "measured"}}},
      {"instrument", "loom.semantic.batch"}, {"model", model}, {"endpoint", batch_endpoint}};
  if (!operation_id.empty()) estimate["operation_id"] = operation_id;
  request.usage.estimate = std::move(estimate);
  request.usage.confirmation = confirmation;
  request.usage.declared_on_success = Json{{"messages", batch_requests.size()}, {"cost_usd", declared_cost}};
  request.hooks.on_dispatch = [&] {  // admitted: the old receipt is spent
    std::lock_guard lk(mu);
    usage_pending.reset();
    usage_confirmation.reset();
  };
  const llm::VariantResult sent = llm::execute_call(services, request, *prepared);

  if (sent.state == llm::State::GateError) {
    log::error("loom.semantic_worker", "Batch usage admission error: {}", sent.error ? sent.error->message : std::string());
    return 0;
  }
  if (sent.notices.is_array() && !sent.notices.empty())  // a spending limit that only tells: the batch goes on, the notice is in the ledger's decision
    log::warn("loom.semantic_worker", "Batch admitted over a spending limit (on_exceed notify): {}", json::dump(sent.notices));
  if (sent.state == llm::State::Held) {
    const auto status = json::get_string(sent.hold, "status", "unavailable");
    const Json& admitted = sent.hold["decision"];
    std::lock_guard lk(mu);
    usage_confirmation.reset();
    if (status == "requires_confirmation") {
      usage_pending = Json{{"schema", "loom.semantic_usage_pending/1"}, {"status", status},
          {"operation_id", admitted.value("operation_id", Json(nullptr))},
          {"receipt_id", admitted.value("receipt_id", Json(nullptr))},
          {"baseline_key", admitted.value("baseline_key", Json(nullptr))},
          {"projected", admitted.value("resources", Json::object())},
          {"messages", batch_requests.size()}, {"body_sha256", body_sha}, {"sent", false}, {"conv_ids", conv_ids}};
      add_limit_reasons(*usage_pending, admitted);
      mode = "awaiting_usage_confirmation";
      log::warn("loom.semantic_worker", "Batch of {} messages awaits usage confirmation (receipt {})",
                batch_requests.size(), json::get_string(admitted, "receipt_id"));
    } else if (status == "denied" && !usage_limit_reasons(admitted).empty()) {
      // P18: a spending limit with on_exceed deny refuses the batch. Nothing is sent and the messages stay pending; the refusal is said (status,
      // conversation) and asked again only after a while (the limit may have been raised or the period may have turned over).
      usage_pending = refusal_pending(admitted, "batch", batch_requests.size(), body_sha, conv_ids, profile.values().value("limit_refusal_retry_ms", std::int64_t{600000}));
      mode = "blocked_by_spend_limit";
      log::warn("loom.semantic_worker", "Batch of {} messages refused by a spending limit", batch_requests.size());
    } else {
      usage_pending.reset();
      log::warn("loom.semantic_worker", "Batch not admitted: {}", status);
    }
    return 0;
  }
  if (sent.state == llm::State::TransportError || sent.state == llm::State::Cancelled ||
      sent.state == llm::State::Unavailable) {
    log::error("loom.semantic_worker", "Batch submit error: {}", sent.error ? sent.error->message : std::string("unavailable"));
    return 0;
  }
  if (sent.state == llm::State::Ok) {
    auto j = json::parse(sent.body);
    std::string id = j ? json::get_string(*j, "id") : std::string();
    std::int64_t submitted = static_cast<std::int64_t>(batch_requests.size());
    double cost = declared_cost;
    {
      std::lock_guard lk(mu);
      batch_id = id;
      batch_submitted = submitted;
      mode = "batch_wait";
    }
    log::info("loom.semantic_worker", "Batch submitted: {} ({} msgs, ~${:.2f})", id, submitted, cost);
    return 0;
  }
  log::warn("loom.semantic_worker", "Batch submit failed {}: {}", sent.http_status, std::string(utf8::prefix(sent.body, profile.values().at("submit_error_body_chars").get<std::size_t>())));
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
  Json out{{"pending", pending},
              {"failed", failed ? Json(*failed) : Json(nullptr)},
              {"executing", executing ? Json(*executing) : Json(nullptr)},
              {"stalled", stalled ? Json(*stalled) : Json(nullptr)},
              {"quarantined", quarantined},
              {"retry_mode", retry_mode},
              {"counts_known", counts_known},
              {"processed", processed},
              {"errors", errors},
              {"mode", mode},
              {"rate", rate_buf},
              {"batch_id", batch_id ? Json(*batch_id) : Json(nullptr)},
              {"batch_submitted", batch_submitted},
              {"running", running},
              {"paused", paused}};
  if (usage_pending) out["usage_pending"] = *usage_pending;
  return out;
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
  retry_mode_ = profile_ ? profile_->values().value("retry_mode", std::string("explicit_requeue")) : "explicit_requeue";
  if (profile_) {
    std::vector<std::string> order;
    for (const auto& n : profile_->values().at("analysis_modes")) order.push_back(n.get<std::string>());
    std::vector<std::string> unknown;
    modes_ = make_analysis_modes(order, AnalysisModeContext{&llm_}, &unknown);
    for (const auto& n : unknown) log::warn("loom.semantic_worker", "analysis_modes: '{}' is not a registered mode (ignored)", n);
  }
  instance_ = gen_id("w_");
  register_attempt_owner(instance_);
  import_sub_ = ScopedSubscription(bus_, bus_.on(events::kImportDone, [this](std::string_view, const Json&) { wake(); }));
}

SemanticWorker::~SemanticWorker() {
  stop();
  unregister_attempt_owner(instance_);
}

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
  scan_cursor_.store(0);  // something changed the queue (import, requeue, settings): sweep from the start again
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
  {
    auto lock = db_.lock();
    auto counts = db_.conn().prepare("SELECT "
      "COUNT(CASE WHEN semantic_status='pending' THEN 1 END), "
      "COUNT(CASE WHEN semantic_status='failed' THEN 1 END), "
      "COUNT(CASE WHEN semantic_status='executing' THEN 1 END), "
      "COUNT(CASE WHEN semantic_status NOT IN ('pending','failed','executing','done') "
      "OR semantic_status IS NULL THEN 1 END) FROM messages");
    if (counts) {
      auto row = counts->step();
      if (row && *row) {
        s.pending = counts->get_int(0);
        s.failed = counts->get_int(1);
        s.executing = counts->get_int(2);
        s.counts_known = counts->get_int(3) == 0;
      }
    }
    if (s.counts_known && profile_) {
      QueueFilter all;
      all.failed = false;
      all.active_only = false;
      auto stalled = scan_queue(db_, profile_->values().at("executing_lease_ms").get<std::int64_t>(), all);
      if (stalled) s.stalled = static_cast<std::int64_t>(stalled->size());
    }
  }
  s.processed = processed_.load();
  s.errors = errors_.load();
  s.paused = paused_.load();
  const std::string retry = effective_retry_mode();
  std::lock_guard lk(mu_);
  s.mode = mode_;
  s.retry_mode = retry;
  s.quarantined = static_cast<std::int64_t>(quarantine_.size());
  s.rate = rate_;
  s.batch_id = batch_id_;
  s.batch_submitted = batch_submitted_;
  s.usage_pending = usage_pending_ ? usage_pending_ : retry_pending_;
  s.running = thread_.joinable();
  return s;
}

Result<Json> SemanticWorker::runtime_profile() const { LOOM_TRY(profile_); return profile_->inspection(); }

void SemanticWorker::announce_pending() {
  Json pending;
  {
    std::lock_guard lk(mu_);
    const std::optional<Json>& slot = usage_pending_ ? usage_pending_ : retry_pending_;
    if (!slot) {
      announced_receipt_.clear();  // nothing waits: the next hold is a new question
      return;
    }
    const std::string receipt = json::get_string(*slot, "receipt_id");
    if (receipt.empty() || receipt == announced_receipt_) return;
    announced_receipt_ = receipt;
    pending = *slot;
    if (!pending.contains("kind")) pending["kind"] = "batch";
  }
  bus_.emit(events::kSemanticUsagePending, pending);
}

Result<Json> SemanticWorker::confirm_usage(bool approved, std::string_view receipt_id, std::string_view ref) {
  Json pending;
  bool is_retry = false;
  {
    std::lock_guard lk(mu_);
    if (!usage_pending_ && !retry_pending_) return Error(Errc::NotFound, "no semantic usage confirmation is pending");
    auto matches = [&](const std::optional<Json>& slot) { return slot && json::get_string(*slot, "receipt_id") == receipt_id; };
    if (matches(usage_pending_)) {
      pending = *usage_pending_;
    } else if (matches(retry_pending_)) {
      pending = *retry_pending_;
      is_retry = true;
    } else {
      return Error(Errc::InvalidArgument, "receipt_id does not match the pending semantic usage receipt");
    }
    if (approved) {
      (is_retry ? retry_confirmation_ : usage_confirmation_) =
          Json{{"approved", true}, {"receipt_id", std::string(receipt_id)}, {"ref", std::string(ref)}};
    }
  }
  const auto tell = [&] {  // whoever shows the question closes it (events::kSemanticUsageAnswered)
    bus_.emit(events::kSemanticUsageAnswered, Json{{"receipt_id", std::string(receipt_id)}, {"operation_id", pending.value("operation_id", Json(nullptr))},
                                                   {"approved", approved}, {"ref", std::string(ref)}, {"kind", is_retry ? "retry" : "batch"},
                                                   {"conv_ids", pending.value("conv_ids", Json::array())}, {"ids", pending.value("ids", Json::array())}});
  };
  if (approved) {
    tell();
    wake();
    return Json{{"status", "confirmation_recorded"}, {"operation_id", pending["operation_id"]},
                {"receipt_id", pending["receipt_id"]}, {"executes", "next drain"}};
  }
  LOOM_TRY_ASSIGN(auto options, effective_usage_policy_options(cfg_));
  LOOM_TRY_ASSIGN(auto policy, UsagePolicy::open(cfg_.path().parent_path() / "usage-policy.sqlite", options));
  LOOM_TRY_ASSIGN(auto denied, policy->confirm(json::get_string(pending, "operation_id"), receipt_id, false, ref));
  if (is_retry) {
    // A "no" answers this operation only: the setting is untouched, the items
    // stay failed (noted usage_declined, no further automatic resend until the
    // owner requeues them) and the worker keeps processing everything else.
    if (const Json* ids = json::find(pending, "ids"); ids && ids->is_array()) {
      for (const auto& id : *ids) {
        auto lock = db_.lock();
        auto message = db_.get_msg(id.get<std::string>());
        if (!message || !*message || !(*message)->metadata.is_object()) continue;
        Json metadata = (*message)->metadata;
        auto it = metadata.find("loom_semantic_attempts");
        if (it == metadata.end() || !it->is_array() || it->empty() || !it->back().is_object()) continue;
        it->back()["usage_declined"] = Json{{"at", timeutil::utc_now_iso()}, {"receipt_id", std::string(receipt_id)},
                                            {"ref", std::string(ref)}};
        MsgPatch patch; patch.metadata = std::move(metadata);
        if (auto st = db_.update_msg(id.get<std::string>(), patch); !st)
          log::warn("loom.semantic_worker", "usage_declined note not written for {}: {}", id.get<std::string>(), st.error().message);
      }
    }
    {
      std::lock_guard lk(mu_);
      retry_pending_.reset();
      retry_confirmation_.reset();
    }
    tell();
    return denied;
  }
  {
    std::lock_guard lk(mu_);
    usage_pending_.reset();
    usage_confirmation_.reset();
  }
  pause();  // the owner declined: do not re-ask in a loop; resume() is explicit
  tell();
  return denied;
}

std::string SemanticWorker::effective_retry_mode() const {
  // config (user setting) -> pack preset; read on every use so a change applies at once
  const Json configured = cfg_.get("semantic_retry_mode");
  if (configured.is_string() && (configured == "explicit_requeue" || configured == "automatic"))
    return configured.get<std::string>();
  std::lock_guard lk(mu_);
  return retry_mode_;
}

Status SemanticWorker::persist_retry_mode(Config& cfg, std::string_view mode) {
  if (mode != "explicit_requeue" && mode != "automatic")
    return Error(Errc::InvalidArgument, "retry_mode must be explicit_requeue or automatic");
  cfg.set("semantic_retry_mode", std::string(mode));
  return cfg.save();
}

Status SemanticWorker::set_retry_mode(std::string_view mode) {
  if (mode != "explicit_requeue" && mode != "automatic")
    return Error(Errc::InvalidArgument, "retry_mode must be explicit_requeue or automatic");
  {
    std::lock_guard lk(mu_);
    retry_mode_ = std::string(mode);
    if (mode != "automatic") { retry_pending_.reset(); retry_confirmation_.reset(); }
  }
  wake();
  return {};
}

Result<Json> SemanticWorker::requeue(const Json& filter) {
  scan_cursor_.store(0);  // requeued rows may sit before the cursor
  LOOM_TRY(profile_);
  LOOM_TRY_ASSIGN(auto f, parse_queue_filter(filter));
  const auto lease = profile_->values().at("executing_lease_ms").get<std::int64_t>();
  LOOM_TRY_ASSIGN(auto items, scan_queue(db_, lease, f));
  Json ids = Json::array(), skipped = Json::array();
  std::int64_t failed_n = 0, stalled_n = 0;
  if (!f.dry_run) {
    for (const auto& item : items) {
      auto done = graph_.requeue_attempt(item.id, "bulk_requeue", [&](const Message& m) {
        if (item.kind == "failed") return m.semantic_status == "failed";
        return m.semantic_status == "executing" && stall_reason(m.metadata, lease).has_value();
      });
      if (!done) {
        skipped.push_back(Json{{"id", item.id}, {"reason", done.error().message}});
      } else if (!*done) {
        skipped.push_back(Json{{"id", item.id}, {"reason", "changed since scan"}});
      } else {
        ids.push_back(item.id);
        (item.kind == "failed" ? failed_n : stalled_n)++;
      }
    }
    if (!ids.empty()) wake();
  } else {
    for (const auto& item : items) ids.push_back(item.id);
  }
  return Json{{"dry_run", f.dry_run}, {"matched", items.size()}, {"requeued", f.dry_run ? 0 : ids.size()},
              {"failed_requeued", failed_n}, {"stalled_requeued", stalled_n},
              {"ids", ids}, {"skipped", skipped}, {"history_preserved", true}};
}

void SemanticWorker::auto_requeue(bool reaches_provider) {
  if (!profile_) return;
  if (effective_retry_mode() != "automatic") {
    std::lock_guard lk(mu_);
    retry_pending_.reset();
    retry_confirmation_.reset();
    return;
  }
  const auto& pv = profile_->values();
  const auto lease = pv.at("executing_lease_ms").get<std::int64_t>();
  const auto spacing = std::chrono::milliseconds(pv.at("retry_spacing_ms").get<std::int64_t>());
  auto scanned = scan_queue(db_, lease, QueueFilter{});
  if (!scanned) { log::error("loom.semantic_worker", "Automatic retry scan failed: {}", scanned.error().message); return; }
  const auto now = timeutil::Clock::now();
  std::vector<QueueItem> due;
  for (auto& item : *scanned) {
    if (item.declined) continue;
    auto when = timeutil::parse_iso_utc(item.when);
    if (!when || now - *when >= spacing) due.push_back(std::move(item));
  }
  if (due.empty()) {
    std::lock_guard lk(mu_);
    retry_pending_.reset();
    retry_confirmation_.reset();
    return;
  }

  std::shared_ptr<llm::UsageGate> gate;
  const double declared_cost = static_cast<double>(due.size()) * pv.at("batch_estimate_per_message").get<double>();
  if (reaches_provider) {
    // A resend reaches the provider: same guard as every other automatic call.
    Json ids = Json::array();
    for (const auto& item : due) ids.push_back(item.id);
    // The guard binds the call this resend leads to: the route's address plus
    // the item set as the body (the analysis itself is sent later, per item).
    const llm::Services services(cfg_, secrets_, http_);
    auto described = llm::describe_request(services, llm::RouteSelector{},
                                           json::dump(Json{{"schema", "loom.semantic_retry/1"}, {"ids", ids}}));
    if (!described) {
      log::error("loom.semantic_worker", "Automatic retry usage admission error: {}", described.error().message);
      return;
    }
    const net::HttpRequest req = std::move(*described);
    const std::string body_sha = Sha256::hex(req.body);
    const std::string model = cfg_string(cfg_, "semantic_model");
    std::string operation_id;
    Json confirmation = Json::object();
    {
      std::lock_guard lk(mu_);
      if (retry_pending_ && json::get_string(*retry_pending_, "body_sha256") != body_sha) {
        retry_pending_.reset();  // the item set changed: an old receipt cannot authorize it
        retry_confirmation_.reset();
      }
      if (refusal_expired(retry_pending_)) {  // P18: the refusal by a spending limit is old: ask the ledger again
        retry_pending_.reset();
        retry_confirmation_.reset();
      }
      if (retry_pending_) {
        if (!retry_confirmation_) return;  // same item set, still awaiting the owner (or refused by a limit): do not re-ask
        operation_id = json::get_string(*retry_pending_, "operation_id");
        confirmation = *retry_confirmation_;
      }
    }
    Json estimate{{"baseline_key", usage_baseline_key(cfg_, "semantic.retry", req.url, model)},
        {"resources", Json{{"messages", due.size()}, {"cost_usd", declared_cost}}},
        {"estimate_provenance", Json{{"cost_usd", "declared"}, {"messages", "counted"}, {"input_bytes", "measured"}}},
        {"instrument", "loom.semantic.retry"}, {"model", model}, {"endpoint", req.url}};
    if (!operation_id.empty()) estimate["operation_id"] = operation_id;
    llm::UsageSpec usage;
    usage.estimate = std::move(estimate);
    usage.confirmation = confirmation;
    gate = llm::make_usage_gate(services, usage);
    auto admitted = gate->admit(req, Json{{"schema", llm::kProvenanceSchema}, {"kind", "semantic.retry"},
                                          {"operation", "requeue"}, {"requested_model", model}});
    if (!admitted) {
      log::error("loom.semantic_worker", "Automatic retry usage admission error: {}", admitted.error().message);
      return;
    }
    if (!admitted->dispatch) {
      const auto status = admitted->status;
      std::lock_guard lk(mu_);
      retry_confirmation_.reset();
      if (status == "requires_confirmation") {
        retry_pending_ = Json{{"schema", "loom.semantic_usage_pending/1"}, {"kind", "retry"}, {"status", status},
            {"operation_id", admitted->decision.value("operation_id", Json(nullptr))},
            {"receipt_id", admitted->decision.value("receipt_id", Json(nullptr))},
            {"baseline_key", admitted->decision.value("baseline_key", Json(nullptr))},
            {"projected", admitted->decision.value("resources", Json::object())},
            {"messages", due.size()}, {"ids", ids}, {"body_sha256", body_sha}, {"sent", false}};
        add_limit_reasons(*retry_pending_, admitted->decision);
        log::warn("loom.semantic_worker", "Automatic resend of {} items awaits usage confirmation (receipt {})",
                  due.size(), json::get_string(admitted->decision, "receipt_id"));
      } else if (status == "denied" && !usage_limit_reasons(admitted->decision).empty()) {
        retry_pending_ = refusal_pending(admitted->decision, "retry", due.size(), body_sha, ids, profile_->values().value("limit_refusal_retry_ms", std::int64_t{600000}));
        mode_ = "blocked_by_spend_limit";
        log::warn("loom.semantic_worker", "Automatic resend refused by a spending limit");
      } else {
        retry_pending_.reset();
        log::warn("loom.semantic_worker", "Automatic resend not admitted: {}", status);
      }
      return;
    }
    std::lock_guard lk(mu_);
    retry_pending_.reset();
    retry_confirmation_.reset();
  }

  std::size_t requeued = 0;
  for (const auto& item : due) {
    auto done = graph_.requeue_attempt(item.id, "automatic", [&](const Message& m) {
      if (item.kind == "failed") return m.semantic_status == "failed";
      return m.semantic_status == "executing" && stall_reason(m.metadata, lease).has_value();
    });
    if (done && *done) ++requeued;
    else if (!done) log::warn("loom.semantic_worker", "Automatic requeue of {} failed: {}", item.id, done.error().message);
  }
  if (gate) gate->settle(llm::GateSettlement{true, 0, Json::object(), Json{{"messages", requeued}, {"cost_usd", declared_cost}}});
  if (requeued) {
    scan_cursor_.store(0);
    log::info("loom.semantic_worker", "Automatic retry requeued {} items", requeued);
  }
}

Result<int> SemanticWorker::drain_once() {
  LOOM_TRY(profile_);
  {
    // Awaiting the owner: nothing is sent, nothing is lost, no retry.
    std::lock_guard lk(mu_);
    if (refusal_expired(usage_pending_)) { usage_pending_.reset(); usage_confirmation_.reset(); }  // P18: ask the ledger again
    if (usage_pending_ && !usage_confirmation_) {
      mode_ = json::get_bool(*usage_pending_, "refused", false) ? "blocked_by_spend_limit" : "awaiting_usage_confirmation";
      return 0;
    }
  }
  AnalysisCapabilities caps;
  caps.llm_ready = llm_.enabled() && cfg_bool(cfg_, "semantic_analysis", true);
  // Whether this pass can reach a provider: the leading per-message mode decides.
  const AnalysisMode* lead = pick_message_mode(modes_, caps, "");
  auto_requeue(lead && lead->sends_to_provider());  // no-op in explicit_requeue mode
  announce_pending();
  std::set<std::string> excluded;
  {
    std::lock_guard lk(mu_);
    excluded = quarantine_;
  }
  // Perf (P19): resume after the last row of the previous batch, so rows that never qualify (short texts, quarantined
  // ids) are walked once per sweep instead of on every batch; an empty sweep wraps to the start once, which also
  // picks up requeued rows with lower rowids.
  auto msgsr = db_.get_unanalysed_msgs(opts_.drain_batch, excluded, scan_cursor_.load());
  if (msgsr && msgsr->empty() && scan_cursor_.load() > 0) {
    scan_cursor_.store(0);
    msgsr = db_.get_unanalysed_msgs(opts_.drain_batch, excluded, 0);
  }
  if (!msgsr) return msgsr.error();
  auto& msgs = *msgsr;
  if (msgs.empty()) return 0;
  for (const auto& pm : msgs) if (pm.rowid > scan_cursor_.load()) scan_cursor_.store(pm.rowid);

  // Resolve once before any transport or write. A malformed overlay is an
  // operation error, never a reason to silently use the built-in analyzer.
  auto graph_profile = RuntimeProfile::load("graph_ingest", db_.path().parent_path());
  if (!graph_profile) { errors_.fetch_add(1); return graph_profile.error(); }
  auto analyzer_result = RuntimeProfile::load("semantic_analyzer", db_.path().parent_path());
  if (!analyzer_result) { errors_.fetch_add(1); return analyzer_result.error(); }
  const auto& analyzer_profile = *analyzer_result;
  // Builtin equality describes the current values, not the analyzer retained
  // by the constructor (which may have been built from a removed overlay).
  std::unique_ptr<SemanticAnalyzer> effective_analyzer;
  if (analyzer_binding_ == AnalyzerBinding::RuntimeProfile || !analyzer_profile.is_builtin()) {
    LOOM_TRY_ASSIGN(effective_analyzer, SemanticAnalyzer::create_with_profile(analyzer_profile));
  }
  const auto& analyzer = effective_analyzer ? *effective_analyzer : regex_;

  auto pending_r = db_.count_pending_semantic();
  std::int64_t pending_total = pending_r.value_or(0);
  bool has_batch_key = !secrets_.get_string(profile_->values().at("batch_secret_key").get<std::string>()).empty();

  caps.batch_ready = pending_total > opts_.batch_threshold && has_batch_key;
  if (AnalysisMode* bulk = pick_bulk_mode(modes_, caps)) {
    struct Host final : AnalysisBulkHost {
      SemanticWorker& w;
      explicit Host(SemanticWorker& worker) : w(worker) {}
      Result<int> submit_batch() override {
        auto done = submit_batch_api(w.db_, w.cfg_, w.secrets_, w.http_, w.mu_, w.mode_, w.batch_id_, w.batch_submitted_,
                                     w.opts_.batch_max_messages, w.opts_.batch_endpoint, *w.profile_, w.usage_pending_,
                                     w.usage_confirmation_);
        w.announce_pending();
        return done;
      }
    } host(*this);
    return bulk->run_bulk(host);
  }
  {
    std::lock_guard lk(mu_);  // the batch path no longer applies: an old receipt is obsolete
    usage_pending_.reset();
    usage_confirmation_.reset();
  }

  std::vector<std::string> used_modes;  // names in order of first use this pass
  {
    std::lock_guard lk(mu_);
    mode_ = lead ? std::string(lead->name()) : "idle";
  }

  double t0 = timeutil::monotonic_seconds();
  int count = 0;

  const std::string retry_mode_now = effective_retry_mode();
  for (const auto& m : msgs) {
    {
      std::lock_guard lk(mu_);
      if (stop_ || paused_.load()) break;
    }
    // A row whose attempt history cannot be extended can never be claimed;
    // it must not stop the rest of the queue (it stays pending and visible).
    if (const Json* history = json::find(m.metadata, "loom_semantic_attempts"); history && !history->is_array()) {
      errors_.fetch_add(1);
      log::warn("loom.semantic_worker", "Skipping {}: attempt history is not an array (data retained)", m.id);
      std::lock_guard lk(mu_);
      quarantine_.insert(m.id);
      continue;
    }
    AnalysisMode* chosen = pick_message_mode(modes_, caps, m.text);
    if (!chosen) {
      errors_.fetch_add(1);
      return Error(Errc::Unavailable, "no analysis mode is available (worker profile analysis_modes)");
    }
    const std::string chosen_name(chosen->name());
    if (std::find(used_modes.begin(), used_modes.end(), chosen_name) == used_modes.end()) {
      used_modes.push_back(chosen_name);
      if (used_modes.size() > 1) {
        std::string joined;
        for (const auto& n : used_modes) joined += (joined.empty() ? "" : "+") + n;
        std::lock_guard lk(mu_);
        mode_ = joined;
      }
    }
    Json provenance{{"consumer", "semantic_worker"}, {"mode", chosen_name},
                    {"worker_profile_hash", profile_->hash()}, {"llm_rate_limit", opts_.llm_rate_limit},
                    {"failure_recovery", profile_->values().at("failure_recovery")},
                    {"analyzer_profile_hash", analyzer.profile_hash()}};
    if (retry_mode_now != "explicit_requeue") provenance["retry_mode"] = retry_mode_now;
    auto claimed = graph_.begin_analysis(m.id, m.text, m.conv_id, *graph_profile, provenance, false, msg_status::kActive,
                                         instance_);
    if (!claimed && (claimed.error().code == Errc::Conflict || claimed.error().code == Errc::NotFound)) {
      // This row changed or vanished under us; others are unaffected.
      errors_.fetch_add(1);
      log::warn("loom.semantic_worker", "Skipping {}: {}", m.id, claimed.error().message);
      continue;
    }
    if (!claimed) {
      errors_.fetch_add(1);
      // No dispatch occurred. Surface the failure to the existing backoff;
      // never create a synthetic done marker for a missing/stale source.
      return claimed.error();
    }
    Json analysis = nullptr;
    Status outcome;
    try {
      analysis = chosen->analyse(claimed->text, analyzer);
      if (!analyzer_profile.is_builtin()) analysis["analyzer_profile_hash"] = analyzer_profile.hash();
      auto complete = graph_.complete_analysis(*claimed, analysis);
      if (!complete) outcome = complete.error();
    } catch (const std::exception& e) {
      outcome = Error(Errc::Internal, "semantic analysis: " + std::string(e.what()));
    }
    if (!outcome) {
      errors_.fetch_add(1);
      auto failed = graph_.fail_analysis(*claimed, outcome.error(), analysis);
      if (!failed) log::error("loom.semantic_worker", "Failure state not written for {}: {}", m.id, failed.error().message);
      log::debug("loom.semantic_worker", "Worker fail on {}: {}", m.id, outcome.error().message);
    } else {
      ++count;
      processed_.fetch_add(1);
    }
    if (chosen->sends_to_provider() && opts_.llm_rate_limit > 0) {
      // Resolve the durable outcome before waiting: stop must not discard an
      // already received result or cause a new dispatch after restart.
      std::unique_lock lk(mu_);
      auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::duration<double>(1.0 / opts_.llm_rate_limit));
      cv_.wait_for(lk, wait, [this] { return stop_; });
      if (stop_) break;
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
        check_batch_status(db_, cfg_, secrets_, http_, bus_, graph_, mu_, mode_, batch_id_, batch_submitted_, processed_, *profile_);
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
