// OWNER: wave 2 net/chat/worker. Port of engine/batch_api.py (SemanticBatchAPI).
#include "loom/batch_api.h"

#include <algorithm>

#include "loom/config.h"
#include "loom/db.h"
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

std::string rstrip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

struct BatchItem {
  std::string msg_id;
  std::string prompt;
};

std::optional<Json> single_request(net::HttpTransport& http, const std::string& base_url, const std::string& api_key,
                                   const std::string& model, const std::string& prompt) {
  Json body{{"model", model},
            {"messages", Json::array({Json{{"role", "user"}, {"content", prompt}}})},
            {"temperature", 0.1},
            {"max_tokens", 800}};
  net::HttpRequest req;
  req.method = "POST";
  req.url = base_url + "/chat/completions";
  req.headers = {{"Authorization", "Bearer " + api_key}, {"Content-Type", "application/json"}};
  req.body = json::dump(body);
  req.timeout_ms = 30000;
  auto resp = http.send(req);
  if (!resp || resp->status != 200) return std::nullopt;
  auto j = resp->json();
  if (!j) return std::nullopt;
  std::string raw;
  try {
    raw = j->at("choices").at(0).at("message").at("content").get<std::string>();
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return SemanticBatchAPI::parse_analysis(raw);
}

void run_concurrent_batch(Database& db, net::HttpTransport& http, std::mutex& mu, Json& batches, std::string batch_id,
                          std::vector<BatchItem> items, std::string base_url, std::string api_key, std::string model) {
  std::atomic<std::size_t> next{0};
  std::atomic<int> applied{0};
  const std::size_t n = items.size();

  auto worker = [&]() {
    while (true) {
      std::size_t i = next.fetch_add(1);
      if (i >= n) break;
      if (auto analysis = single_request(http, base_url, api_key, model, items[i].prompt)) {
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

Json check_anthropic_batch(const Secrets& secrets, net::HttpTransport& http, std::string_view batch_id) {
  net::HttpRequest req;
  req.method = "GET";
  req.url = "https://api.anthropic.com/v1/messages/batches/" + std::string(batch_id);
  req.headers = {{"x-api-key", secrets.get_string("api_key")}, {"anthropic-version", "2023-06-01"}};
  req.timeout_ms = 30000;
  auto resp = http.send(req);
  if (resp && resp->status == 200) {
    if (auto j = resp->json()) {
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

SemanticBatchAPI::SemanticBatchAPI(const Config& cfg, const Secrets& secrets, Database& db, net::HttpTransport& http,
                                   const SemanticAnalyzer& regex, TaskEngine* tasks)
    : cfg_(cfg), secrets_(secrets), db_(db), http_(http), regex_(regex), tasks_(tasks) {}

SemanticBatchAPI::~SemanticBatchAPI() {
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
}

Result<std::optional<std::string>> SemanticBatchAPI::submit_anthropic_batch(const std::vector<std::string>& msg_ids,
                                                                            std::string_view model_in) {
  std::string key = secrets_.get_string("api_key");
  std::string model(model_in);
  if (model.empty()) model = cfg_string(cfg_, "semantic_model");
  if (key.empty() || model.empty()) {
    log::warn("loom.batch_api", "Batch API: missing API key or model");
    return std::optional<std::string>(std::nullopt);
  }

  std::vector<BatchItem> items;
  items.reserve(msg_ids.size());
  for (const auto& mid : msg_ids) {
    auto m = db_.get_msg(mid);
    if (!m || !*m || (*m)->text.empty()) continue;
    items.push_back(BatchItem{mid, std::string(kAnalysisPrompt) + std::string(utf8::prefix((*m)->text, 3000))});
  }
  if (items.empty()) return std::optional<std::string>(std::nullopt);

  std::string base_url = rstrip_slash(cfg_string(cfg_, "base_url"));
  bool is_anthropic_direct = base_url.find("anthropic.com") != std::string::npos;

  if (is_anthropic_direct) {
    Json requests_list = Json::array();
    for (const auto& it : items) {
      requests_list.push_back(
          Json{{"custom_id", it.msg_id},
              {"params", Json{{"model", model},
                             {"max_tokens", 800},
                             {"messages", Json::array({Json{{"role", "user"}, {"content", it.prompt}}})}}}});
    }
    net::HttpRequest req;
    req.method = "POST";
    req.url = "https://api.anthropic.com/v1/messages/batches";
    req.headers = {{"x-api-key", key}, {"anthropic-version", "2023-06-01"}, {"Content-Type", "application/json"}};
    req.body = json::dump(Json{{"requests", requests_list}});
    req.timeout_ms = 60000;
    auto resp = http_.send(req);
    if (!resp) {
      log::error("loom.batch_api", "Anthropic batch submit error: {}", resp.error().message);
      return std::optional<std::string>(std::nullopt);
    }
    if (resp->status == 200 || resp->status == 201) {
      auto j = resp->json();
      if (!j) return std::optional<std::string>(std::nullopt);
      std::string id = json::get_string(*j, "id");
      {
        std::lock_guard lk(mu_);
        batches_[id] = Json{{"type", "anthropic"}, {"count", items.size()}, {"status", "processing"}};
      }
      log::info("loom.batch_api", "Anthropic batch submitted: {} ({} requests)", id, items.size());
      return std::optional<std::string>(id);
    }
    log::warn("loom.batch_api", "Anthropic batch submit failed {}: {}", resp->status,
             std::string(utf8::prefix(resp->body, 200)));
    return std::optional<std::string>(std::nullopt);
  }

  std::string batch_id = "concurrent_" + std::to_string(static_cast<long long>(timeutil::unix_millis() / 1000));
  {
    std::lock_guard lk(mu_);
    batches_[batch_id] = Json{{"type", "concurrent"}, {"count", items.size()}, {"status", "processing"}, {"applied", 0}};
  }
  {
    std::lock_guard lk(mu_);
    workers_.emplace_back(run_concurrent_batch, std::ref(db_), std::ref(http_), std::ref(mu_), std::ref(batches_),
                          batch_id, std::move(items), base_url, key, model);
  }
  return std::optional<std::string>(batch_id);
}

Json SemanticBatchAPI::check_batch(std::string_view batch_id) {
  Json info;
  {
    std::lock_guard lk(mu_);
    auto it = batches_.find(std::string(batch_id));
    if (it == batches_.end()) return Json{{"status", "unknown"}, {"batch_id", std::string(batch_id)}};
    info = *it;
  }
  std::string type = json::get_string(info, "type");
  if (type == "anthropic") return check_anthropic_batch(secrets_, http_, batch_id);
  if (type == "concurrent") return info;
  return Json{{"status", "unknown"}};
}

Result<int> SemanticBatchAPI::collect_results(std::string_view batch_id) {
  Json info;
  {
    std::lock_guard lk(mu_);
    auto it = batches_.find(std::string(batch_id));
    if (it != batches_.end()) info = *it;
  }
  if (json::get_string(info, "type") == "concurrent") {
    return static_cast<int>(json::get_int(info, "applied", 0));
  }

  Json status = check_anthropic_batch(secrets_, http_, batch_id);
  const Json* ru = json::find(status, "results_url");
  if (!ru || !ru->is_string() || ru->get_ref<const std::string&>().empty()) {
    log::warn("loom.batch_api", "No results URL for batch {}", batch_id);
    return 0;
  }

  net::HttpRequest req;
  req.method = "GET";
  req.url = ru->get<std::string>();
  req.headers = {{"x-api-key", secrets_.get_string("api_key")}, {"anthropic-version", "2023-06-01"}};
  req.timeout_ms = 120000;
  req.stream = true;

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
      auto analysis = parse_analysis(content);
      if (analysis) {
        (void)db_.mark_analysed(msg_id, *analysis);
        ++applied;
      }
    } catch (const std::exception&) {
      log::debug("loom.batch_api", "Failed to parse batch result line for {}", msg_id);
    }
  };

  net::StreamSink sink;
  sink.on_data = [&](std::string_view chunk) -> bool {
    buf.append(chunk);
    std::size_t pos;
    while ((pos = buf.find('\n')) != std::string::npos) {
      process_line(std::string_view(buf).substr(0, pos));
      buf.erase(0, pos + 1);
    }
    return true;
  };
  auto resp = http_.send(req, &sink);
  if (!buf.empty()) process_line(buf);
  if (!resp) {
    log::error("loom.batch_api", "Failed to collect batch results: {}", resp.error().message);
    return 0;
  }
  log::info("loom.batch_api", "Batch {}: applied {} results", batch_id, applied);
  return applied;
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
