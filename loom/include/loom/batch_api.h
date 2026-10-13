// loom/batch_api.h — port of engine/batch_api.py (SemanticBatchAPI).
//                                                           [OWNER: wave 2 net/chat/worker]
// Bulk semantic analysis:
//   submit_anthropic_batch(msg_ids, model=""): model defaults to
//     config.semantic_model; needs secrets.api_key (Python) — texts via the
//     message id; each request {"custom_id": id, "params": {"model",
//     "max_tokens":800, "messages":[{"role":"user","content": kAnalysisPrompt
//     + text[:3000]}]}}. If config.base_url names the host of a provider
//     manifest offering batch/messages.batches (builtin: anthropic): POST
//     https://api.anthropic.com/v1/messages/batches (x-api-key,
//     anthropic-version 2023-06-01, timeout 60; all built by llm::use_model's
//     anthropic_messages adapter) -> batch id; else the
//     "concurrent" path: requests run on 3 worker threads against
//     {base_url}/chat/completions (temperature 0.1, max_tokens 800, timeout
//     30) and results are applied with mark_analysed; returns
//     "concurrent_<unix seconds>".
//   check_batch(id): unknown -> {"status":"unknown","batch_id"}; concurrent
//     -> its info dict; anthropic -> GET .../batches/{id} ->
//     {"batch_id","status":processing_status,"succeeded","errored","total":
//     processing+succeeded,"results_url"} or {"batch_id","status":"error"}.
//   collect_results(id): concurrent -> applied count; anthropic -> stream
//     results_url JSONL; succeeded lines -> parse_analysis(content[0].text)
//     -> mark_analysed. Returns applied count.
//   batch_regex(limit=5000): get_unanalysed_msgs(limit); blank text ->
//     mark_analysed({"source":"skip"}); else regex unified analysis WITHOUT
//     "relations" (Python quirk) -> mark_analysed. Returns analysed count.
//   parse_analysis(raw): fence stripping + JSON; sets "source":"llm".
// Loom: when a TaskEngine is given, the concurrent path runs as a resumable
// task of kind "semantic.batch" instead of a detached thread.
#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "loom/adapter_registry.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Config;
class Secrets;
class Database;
class SemanticAnalyzer;
class TaskEngine;
namespace net {
class HttpTransport;
}

namespace llm {
struct Services;
}

struct BatchItem {
  std::string msg_id;
  std::string prompt;
};

// What a batch backend may use of the API that owns it.
struct BatchHost {
  const Config& cfg;
  const Secrets& secrets;
  Database& db;
  net::HttpTransport& http;
  std::mutex& mu;                   // guards `batches`
  Json& batches;                    // id -> record ({"type": backend name, "status", ...})
  std::vector<std::thread>& workers;
};
struct BatchHostRef {  // the registry's environment type (cheap to copy)
  BatchHost* host = nullptr;
};

// One way to run a batch of analysis requests.  The registry key is the record's
// "type"; a backend is chosen by the address (serves) and later found again by the
// type stored in its record.
class BatchBackend {
 public:
  virtual ~BatchBackend() = default;
  virtual bool serves(const llm::Services& services, const std::string& base_url) = 0;
  virtual Result<std::optional<std::string>> submit(const llm::Services& services, const std::vector<BatchItem>& items,
                                                    const std::string& base_url, const std::string& model) = 0;
  virtual Json check(const llm::Services& services, const Json& info, std::string_view batch_id) = 0;
  virtual Result<int> collect(const llm::Services& services, const Json& info, std::string_view batch_id) = 0;
};
using BatchBackendRegistry = AdapterRegistry<BatchBackend, BatchHostRef>;

class SemanticBatchAPI {
 public:
  SemanticBatchAPI(const Config& cfg, const Secrets& secrets, Database& db, net::HttpTransport& http,
                   const SemanticAnalyzer& regex, TaskEngine* tasks = nullptr);
  ~SemanticBatchAPI();  // joins concurrent workers

  Result<std::optional<std::string>> submit_anthropic_batch(const std::vector<std::string>& msg_ids,
                                                            std::string_view model = "");
  Json check_batch(std::string_view batch_id);
  Result<int> collect_results(std::string_view batch_id);
  Result<int> batch_regex(int limit = 5000);
  static std::optional<Json> parse_analysis(std::string_view raw);
  Json active_batches() const;
  // Registration order is the order backends are asked to serve an address; entries
  // whose meta has "catch_all": true (the built-in "concurrent") are asked last.
  static BatchBackendRegistry& backends();

 private:
  const Config& cfg_;
  const Secrets& secrets_;
  Database& db_;
  net::HttpTransport& http_;
  const SemanticAnalyzer& regex_;
  TaskEngine* tasks_;
  mutable std::mutex mu_;
  Json batches_ = Json::object();  // id -> info
  std::vector<std::thread> workers_;
  BatchHost host_;
};

}  // namespace loom
