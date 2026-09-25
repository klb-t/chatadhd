// OWNER: wave 2 net/chat/worker. Stub.
#include "loom/batch_api.h"

#include "stub.h"

namespace loom {

SemanticBatchAPI::SemanticBatchAPI(const Config& cfg, const Secrets& secrets, Database& db, net::HttpTransport& http,
                                   const SemanticAnalyzer& regex, TaskEngine* tasks)
    : cfg_(cfg), secrets_(secrets), db_(db), http_(http), regex_(regex), tasks_(tasks) {}

SemanticBatchAPI::~SemanticBatchAPI() {
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
}

Result<std::optional<std::string>> SemanticBatchAPI::submit_anthropic_batch(const std::vector<std::string>&,
                                                                            std::string_view) {
  return LOOM_NOT_IMPLEMENTED("SemanticBatchAPI::submit_anthropic_batch");  // STUB: wave2
}
Json SemanticBatchAPI::check_batch(std::string_view batch_id) {
  return Json{{"status", "unknown"}, {"batch_id", std::string(batch_id)}};  // STUB: wave2
}
Result<int> SemanticBatchAPI::collect_results(std::string_view) {
  return LOOM_NOT_IMPLEMENTED("SemanticBatchAPI::collect_results");  // STUB: wave2
}
Result<int> SemanticBatchAPI::batch_regex(int) {
  return LOOM_NOT_IMPLEMENTED("SemanticBatchAPI::batch_regex");  // STUB: wave2
}
std::optional<Json> SemanticBatchAPI::parse_analysis(std::string_view) { return std::nullopt; }  // STUB: wave2
Json SemanticBatchAPI::active_batches() const {
  std::lock_guard lk(mu_);
  return batches_;
}

}  // namespace loom
