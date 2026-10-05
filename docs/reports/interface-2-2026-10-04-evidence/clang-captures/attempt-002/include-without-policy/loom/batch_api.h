// loom/batch_api.h — port of engine/batch_api.py (SemanticBatchAPI).
//                                                           [OWNER: wave 2 net/chat/worker]
// Bulk semantic analysis:
//   submit_anthropic_batch(msg_ids, model=""): model defaults to
//     config.semantic_model; needs secrets.api_key (Python) — texts via the
//     message id; each request {"custom_id": id, "params": {"model",
//     "max_tokens":800, "messages":[{"role":"user","content": kAnalysisPrompt
//     + text[:3000]}]}}. If config.base_url contains "anthropic.com": POST
//     https://api.anthropic.com/v1/messages/batches (x-api-key,
//     anthropic-version 2023-06-01, timeout 60) -> batch id; else the
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
};

}  // namespace loom
