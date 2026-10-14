// OWNER: wave 2 net/chat/worker. Port of engine/batch_api.py (SemanticBatchAPI).
#include "loom/annotation_layer.h"
#include "loom/batch_api.h"

#include <algorithm>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/usage_policy.h"
#include "loom/use_model.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"
#include "stub.h"

namespace loom {

namespace {

std::string cfg_string(const Config& cfg, std::string_view key, std::string fallback = "") {
  Json v = cfg.get(key);
  return v.is_string() ? v.get<std::string>() : std::move(fallback);
}

std::string rstrip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

std::optional<Json> single_request(const llm::Services& services, const std::string& base_url,
                                   const std::string& model, const std::string& prompt) {
  llm::ModelRequest request;
  request.kind = "batch.concurrent";
  request.action = "batch.chat";
  request.models = {llm::ModelSpec{model, "", Json::object()}};
  request.prompt = prompt;
  request.parameters = Json{{"temperature", 0.1}, {"max_tokens", 800}};
  request.route.base_url = base_url;
  request.referer = false;  // this path never sent attribution headers
  request.timeout_ms = 30000;
  auto outcome = llm::use_model(services, std::move(request));
  if (!outcome) {
    log::warn("loom.batch_api", "Concurrent batch request not made: {} ({})", outcome.error().message,
              errc_name(outcome.error().code));
    return std::nullopt;
  }
  if (!outcome->ok()) {
    const llm::VariantResult& failed = outcome->last();
    log::warn("loom.batch_api", "Concurrent batch request failed: {} ({}){}", llm::to_string(failed.state),
              failed.error ? std::string(errc_name(failed.error->code)) : "no error code",
              failed.error ? ": " + failed.error->message
              : failed.state == llm::State::HttpError ? ": HTTP " + std::to_string(failed.http_status) : std::string());
    return std::nullopt;
  }
  auto analysis = SemanticBatchAPI::parse_analysis(outcome->last().text);
  if (!analysis) log::warn("loom.batch_api", "Concurrent batch request returned no usable analysis JSON");
  return analysis;
}

void run_concurrent_batch(Database& db, const Config& cfg, const Secrets& secrets, net::HttpTransport& http,
                          std::mutex& mu, Json& batches, std::string batch_id, std::vector<BatchItem> items,
                          std::string base_url, std::string model) {
  const llm::Services services(cfg, secrets, http);
  std::atomic<std::size_t> next{0};
  std::atomic<int> applied{0};
  const std::size_t n = items.size();

  auto worker = [&]() {
    while (true) {
      std::size_t i = next.fetch_add(1);
      if (i >= n) break;
      if (auto analysis = single_request(services, base_url, model, items[i].prompt)) {
        (void)db.mark_analysed(items[i].msg_id, *analysis);
        applied.fetch_add(1);
      }
    }
  };

  std::vector<std::thread> pool;
  std::size_t nworkers = std::min<std::size_t>(3, n);
  pool.reserve(nworkers);
  for (std::size_t i = 0; i < nworkers; ++i) pool.emplace_back(worker);
  for (auto& t : pool) t.join();

  {
    std::lock_guard lk(mu);
    if (batches.contains(batch_id)) {
      batches[batch_id]["status"] = "completed";
      batches[batch_id]["applied"] = applied.load();
    }
  }
  log::info("loom.batch_api", "Concurrent batch {}: applied {}/{}", batch_id, applied.load(), n);
}

// Native batch status through the batch provider's adapter (no spend: no guard).
Json check_anthropic_batch(const llm::Services& services, const std::string& provider, std::string_view batch_id) {
  llm::ModelRequest request;
  request.kind = "batch.status";
  request.action = "batch.chat";
  request.models = {llm::ModelSpec{}};
  request.route.provider = provider;
  request.route.secret = "api_key";  // this path has always used the chat key
  request.operation = llm::Operation::BatchStatus;
  request.target = std::string(batch_id);
  request.timeout_ms = 30000;
  request.usage.guard = false;
  auto outcome = llm::use_model(services, std::move(request));
  if (outcome && outcome->last().state == llm::State::Ok) {
    if (auto j = json::parse(outcome->last().body)) {
      std::string status = json::get_string(*j, "processing_status", "unknown");
      const Json* counts = json::find(*j, "request_counts");
      std::int64_t succeeded = counts ? json::get_int(*counts, "succeeded", 0) : 0;
      std::int64_t errored = counts ? json::get_int(*counts, "errored", 0) : 0;
      std::int64_t processing = counts ? json::get_int(*counts, "processing", 0) : 0;
      Json out{{"batch_id", std::string(batch_id)},
              {"status", status},
              {"succeeded", succeeded},
              {"errored", errored},
              {"total", processing + succeeded}};
      const Json* ru = json::find(*j, "results_url");
      out["results_url"] = (ru && !ru->is_null()) ? *ru : Json(nullptr);
      return out;
    }
  } else {
    log::debug("loom.batch_api", "Batch check failed for {}", batch_id);
  }
  return Json{{"batch_id", std::string(batch_id)}, {"status", "error"}};
}

}  // namespace

// ── Batch backends: the registry's entries ───────────────────────────
// The address picks the backend: the first entry (registration order) that
// serves it.  A backend writes its own record into the host's batch table
// (including "type" = its registry name) and answers check / collect for the
// records it wrote; nothing outside this registry spells a backend's name.
namespace {

class NativeBatchBackend final : public BatchBackend {
 public:
  NativeBatchBackend(std::string name, BatchHost& host) : name_(std::move(name)), host_(host) {}

  // A manifest offering batch/messages.batches at the configured host.
  bool serves(const llm::Services& services, const std::string& base_url) override {
    provider_ = llm::provider_for_address(services, "batch", "messages.batches", base_url);
    return !provider_.empty();
  }

  Result<std::optional<std::string>> submit(const llm::Services& services, const std::vector<BatchItem>& items,
                                            const std::string& base_url, const std::string& model) override {
    Json requests_list = Json::array();
    for (const auto& it : items) {
      requests_list.push_back(
          Json{{"custom_id", it.msg_id},
              {"params", Json{{"model", model},
                             {"max_tokens", 800},
                             {"messages", Json::array({Json{{"role", "user"}, {"content", it.prompt}}})}}}});
    }
    llm::ModelRequest request;
    request.kind = "batch.submit";
    request.action = "batch.chat";
    request.models = {llm::ModelSpec{}};
    request.route.provider = provider_;
    request.route.secret = "api_key";  // this path has always used the chat key
    request.operation = llm::Operation::BatchSubmit;
    request.batch_requests = std::move(requests_list);
    request.timeout_ms = 60000;
    request.usage.estimate = Json{{"baseline_key", usage_baseline_key(host_.cfg, "batch.submit", base_url, model)},
                                  {"resources", Json{{"messages", items.size()}}},
                                  {"instrument", "loom.batch_api"}, {"model", model}, {"endpoint", base_url}};
    auto outcome = llm::use_model(services, std::move(request));
    if (!outcome) return outcome.error();
    const llm::VariantResult& sent = outcome->last();
    if (sent.state == llm::State::Held) {
      log::warn("loom.batch_api", "Anthropic batch held by the usage guard: {}", json::get_string(sent.hold, "status"));
      return std::optional<std::string>(std::nullopt);
    }
    if (sent.state == llm::State::GateError || sent.state == llm::State::TransportError ||
        sent.state == llm::State::Unavailable) {
      log::error("loom.batch_api", "Anthropic batch submit error: {}",
                 sent.error ? sent.error->message : std::string("unavailable"));
      return std::optional<std::string>(std::nullopt);
    }
    if (sent.state == llm::State::Ok) {
      auto j = json::parse(sent.body);
      if (!j) return std::optional<std::string>(std::nullopt);
      std::string id = json::get_string(*j, "id");
      {
        std::lock_guard lk(host_.mu);
        host_.batches[id] = Json{{"type", name_}, {"provider", provider_}, {"count", items.size()},
                            {"status", "processing"}};
      }
      log::info("loom.batch_api", "Anthropic batch submitted: {} ({} requests)", id, items.size());
      return std::optional<std::string>(id);
    }
    log::warn("loom.batch_api", "Anthropic batch submit failed {}: {}", sent.http_status,
             std::string(utf8::prefix(sent.body, 200)));
    return std::optional<std::string>(std::nullopt);
  }

  Json check(const llm::Services& services, const Json& info, std::string_view batch_id) override {
    return check_anthropic_batch(services, json::get_string(info, "provider"), batch_id);
  }

  Result<int> collect(const llm::Services& services, const Json& info, std::string_view batch_id) override {
  std::string provider = json::get_string(info, "provider");
  if (provider.empty())  // a batch id from an earlier process: the address still names the provider
    provider = llm::provider_for_address(services, "batch", "messages.batches", cfg_string(host_.cfg, "base_url"));
  Json status = check_anthropic_batch(services, provider, batch_id);
  const Json* ru = json::find(status, "results_url");
  if (!ru || !ru->is_string() || ru->get_ref<const std::string&>().empty()) {
    log::warn("loom.batch_api", "No results URL for batch {}", batch_id);
    return 0;
  }

  int applied = 0;
  std::string buf;
  auto process_line = [&](std::string_view line) {
    if (line.empty()) return;
    auto j = json::parse(line);
    if (!j) return;
    std::string msg_id = json::get_string(*j, "custom_id");
    const Json* result = json::find(*j, "result");
    if (!result || json::get_string(*result, "type") != "succeeded") return;
    try {
      std::string content = result->at("message").at("content").at(0).at("text").get<std::string>();
      auto analysis = SemanticBatchAPI::parse_analysis(content);
      if (analysis) {
        (void)host_.db.mark_analysed(msg_id, *analysis);
        ++applied;
      }
    } catch (const std::exception&) {
      log::debug("loom.batch_api", "Failed to parse batch result line for {}", msg_id);
    }
  };

  // The results are JSONL: lines are applied as they arrive, nothing is retained.
  llm::ModelRequest request;
  request.kind = "batch.results";
  request.action = "batch.chat";
  request.models = {llm::ModelSpec{}};
  request.route.provider = provider;
  request.route.secret = "api_key";
  request.operation = llm::Operation::BatchResults;
  request.target = ru->get<std::string>();
  request.timeout_ms = 120000;
  request.stream = true;
  request.capture = true;
  request.retain_body = false;
  request.usage.guard = false;
  request.hooks.on_raw = [&](std::string_view chunk) {
    buf.append(chunk);
    std::size_t pos;
    while ((pos = buf.find('\n')) != std::string::npos) {
      process_line(std::string_view(buf).substr(0, pos));
      buf.erase(0, pos + 1);
    }
  };
  auto outcome = llm::use_model(services, std::move(request));
  if (!buf.empty()) process_line(buf);
  if (!outcome || outcome->last().error) {
    log::error("loom.batch_api", "Failed to collect batch results: {}",
               outcome ? outcome->last().error->message : outcome.error().message);
    return 0;
  }
  log::info("loom.batch_api", "Batch {}: applied {} results", batch_id, applied);
  return applied;
  }

 private:
  std::string name_;
  BatchHost& host_;
  std::string provider_;
};

// Concurrent chat-completions requests: the catch-all (serves every address).
class ConcurrentBatchBackend final : public BatchBackend {
 public:
  ConcurrentBatchBackend(std::string name, BatchHost& host) : name_(std::move(name)), host_(host) {}
  bool serves(const llm::Services&, const std::string&) override { return true; }

  Result<std::optional<std::string>> submit(const llm::Services&, const std::vector<BatchItem>& items_in,
                                            const std::string& base_url, const std::string& model) override {
    std::vector<BatchItem> items = items_in;
  std::string batch_id = "concurrent_" + std::to_string(static_cast<long long>(timeutil::unix_millis() / 1000));
  {
    std::lock_guard lk(host_.mu);
    host_.batches[batch_id] = Json{{"type", name_}, {"count", items.size()}, {"status", "processing"}, {"applied", 0}};
  }
  {
    std::lock_guard lk(host_.mu);
    host_.workers.emplace_back(run_concurrent_batch, std::ref(host_.db), std::cref(host_.cfg), std::cref(host_.secrets), std::ref(host_.http),
                          std::ref(host_.mu), std::ref(host_.batches), batch_id, std::move(items), base_url, model);
  }
  return std::optional<std::string>(batch_id);
  }

  Json check(const llm::Services&, const Json& info, std::string_view) override { return info; }

  Result<int> collect(const llm::Services&, const Json& info, std::string_view) override {
    return static_cast<int>(json::get_int(info, "applied", 0));
  }

 private:
  std::string name_;
  BatchHost& host_;
};

}  // namespace

namespace {
// The backend that serves the address: specific entries in registration order, then the catch-alls.
Result<std::unique_ptr<BatchBackend>> choose_backend(const BatchHostRef& ref, const llm::Services& services,
                                                     const std::string& base_url, std::string* name) {
  for (const bool catch_all : {false, true}) {
    for (const auto& entry : SemanticBatchAPI::backends().entries()) {
      if (!entry.factory || json::get_bool(entry.meta, "catch_all", false) != catch_all) continue;
      auto backend = entry.factory(entry.name, Json::object(), ref);
      if (!backend) return backend.error();
      if (!(*backend)->serves(services, base_url)) continue;
      if (name) *name = entry.name;
      return backend;
    }
  }
  return std::unique_ptr<BatchBackend>();
}
}  // namespace

BatchBackendRegistry& SemanticBatchAPI::backends() {
  static BatchBackendRegistry r = [] {
    BatchBackendRegistry x("type", "batch type");
    x.add({"anthropic", "implemented",
           [](const std::string& name, const Json&, const BatchHostRef& host) -> Result<std::unique_ptr<BatchBackend>> {
             return std::unique_ptr<BatchBackend>(std::make_unique<NativeBatchBackend>(name, *host.host));
           }});
    x.add({"concurrent", "implemented",
           [](const std::string& name, const Json&, const BatchHostRef& host) -> Result<std::unique_ptr<BatchBackend>> {
             return std::unique_ptr<BatchBackend>(std::make_unique<ConcurrentBatchBackend>(name, *host.host));
           },
           Json{{"catch_all", true}}});
    return x;
  }();
  return r;
}

SemanticBatchAPI::SemanticBatchAPI(const Config& cfg, const Secrets& secrets, Database& db, net::HttpTransport& http,
                                   const SemanticAnalyzer& regex, TaskEngine* tasks)
    : cfg_(cfg), secrets_(secrets), db_(db), http_(http), regex_(regex), tasks_(tasks),
      host_{cfg, secrets, db, http, mu_, batches_, workers_} {}

SemanticBatchAPI::~SemanticBatchAPI() {
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
}

Result<std::optional<std::string>> SemanticBatchAPI::submit_anthropic_batch(const std::vector<std::string>& msg_ids,
                                                                            std::string_view model_in) {
  const llm::Services services(cfg_, secrets_, http_);
  std::string model(model_in);
  if (model.empty()) model = cfg_string(cfg_, "semantic_model");
  if (!llm::route_available(services, llm::RouteSelector{}) || model.empty()) {
    log::warn("loom.batch_api", "Batch API: missing API key or model");
    return std::optional<std::string>(std::nullopt);
  }

  std::vector<BatchItem> items;
  items.reserve(msg_ids.size());
  for (const auto& mid : msg_ids) {
    auto m = db_.get_msg(mid);
    if (!m || !*m || (*m)->text.empty()) continue;
    items.push_back(BatchItem{mid, annotations::extend_prompt(kAnalysisPrompt, annotations::segments_plan(cfg_), cfg_) + std::string(utf8::prefix((*m)->text, 3000))});
  }
  if (items.empty()) return std::optional<std::string>(std::nullopt);

  std::string base_url = rstrip_slash(cfg_string(cfg_, "base_url"));
  // The address picks the mode: the first registered backend that serves it.
  const BatchHostRef ref{&host_};
  auto backend = choose_backend(ref, services, base_url, nullptr);
  if (!backend) return backend.error();
  if (!*backend) {
    log::warn("loom.batch_api", "No batch backend serves {}", base_url);
    return std::optional<std::string>(std::nullopt);
  }
  return (*backend)->submit(services, items, base_url, model);
}

Json SemanticBatchAPI::check_batch(std::string_view batch_id) {
  Json info;
  {
    std::lock_guard lk(mu_);
    auto it = batches_.find(std::string(batch_id));
    if (it == batches_.end()) return Json{{"status", "unknown"}, {"batch_id", std::string(batch_id)}};
    info = *it;
  }
  const std::string type = json::get_string(info, "type");
  auto backend = backends().create_named(type, type, Json::object(), BatchHostRef{&host_});
  if (!backend) return Json{{"status", "unknown"}};
  return (*backend)->check(llm::Services(cfg_, secrets_, http_), info, batch_id);
}

Result<int> SemanticBatchAPI::collect_results(std::string_view batch_id) {
  Json info;
  {
    std::lock_guard lk(mu_);
    auto it = batches_.find(std::string(batch_id));
    if (it != batches_.end()) info = *it;
  }
  const llm::Services services(cfg_, secrets_, http_);
  const BatchHostRef ref{&host_};
  std::string type = json::get_string(info, "type");
  if (type.empty()) {  // a batch id from an earlier process: the address still names its backend
    const std::string base_url = rstrip_slash(cfg_string(cfg_, "base_url"));
    std::string found;
    auto probe = choose_backend(ref, services, base_url, &found);
    if (probe && *probe) type = found;
  }
  auto backend = backends().create_named(type, type, Json::object(), ref);
  if (!backend) return 0;
  return (*backend)->collect(services, info, batch_id);
}

Result<int> SemanticBatchAPI::batch_regex(int limit) {
  auto msgs = db_.get_unanalysed_msgs(limit);
  if (!msgs) return msgs.error();
  int count = 0;
  for (const auto& m : *msgs) {
    if (utf8::is_blank(m.text)) {
      (void)db_.mark_analysed(m.id, Json{{"source", "skip"}});
      continue;
    }
    Analysis a = regex_.analyse(m.text);
    Json entities = Json::array();
    for (const auto& e : a.entities) entities.push_back(Json{{"name", e.text}, {"kind", e.entity_type}, {"relevance", e.confidence}});
    Json topics = Json::array();
    for (const auto& t : a.topics) topics.push_back(Json{{"label", t}, {"confidence", 0.5}});
    Json analysis{{"entities", entities}, {"topics", topics}, {"summary", ""}, {"sentiment", "neutral"}, {"source", "regex"}};
    (void)db_.mark_analysed(m.id, analysis);
    ++count;
  }
  log::info("loom.batch_api", "Regex batch: analysed {} messages", count);
  return count;
}

std::optional<Json> SemanticBatchAPI::parse_analysis(std::string_view raw) {
  auto parsed = SemanticLLM::parse_response_json(raw);
  if (!parsed || !parsed->is_object()) return std::nullopt;
  Json result = *parsed;
  result["source"] = "llm";
  return result;
}

Json SemanticBatchAPI::active_batches() const {
  std::lock_guard lk(mu_);
  return batches_;
}

}  // namespace loom
