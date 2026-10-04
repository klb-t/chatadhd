// Source-private provider/cache integration; no public ABI additions.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "loom/context_retrieval.h"
#include "../providers/embedding.h"

namespace loom {
class Runtime;
namespace context {
class ContextEngine;

class ProviderVectorCache {
 public:
  explicit ProviderVectorCache(std::filesystem::path directory = {});
  Result<std::optional<std::vector<float>>> get(std::string_view identity, std::string_view hash);
  Status put(std::string_view identity, std::string_view hash, const std::vector<float>& vector,
             const std::optional<std::string>& reported_model = std::nullopt,
             const Json& response_source = Json::object());
  // Representation integrity is shared by all calls/spaces and persisted
  // transactionally when this cache has a directory. Unknown is not attested.
  Status observe(std::string_view identity, std::size_t dimensions,
                 const std::optional<std::string>& reported_model = std::nullopt);
  Result<Json> inspection(std::string_view identity);
  std::size_t size() const;

 private:
  std::filesystem::path directory_;
  mutable std::mutex mutex_;
  std::map<std::string, std::vector<float>, std::less<>> vectors_;
  std::map<std::string, Json, std::less<>> representations_;
  std::map<std::string, std::optional<std::string>, std::less<>> reported_models_;
  Status observe_locked(std::string_view identity, std::size_t dimensions,
                        const std::optional<std::string>& reported_model);
};

struct ProviderVectorPolicy {
  std::function<Result<Json>(const Json&)> admit;
  std::function<Result<Json>(std::string_view, const Json&)> complete;
  std::function<Result<Json>(std::string_view, std::string_view)> cancel;
  // Declared estimates are never presented as measured token usage or cost.
  Json estimated_resources = Json{{"requests", 1}, {"input_tokens", nullptr}, {"cost_usd", nullptr}};
  std::optional<double> input_tokens_per_byte;
  std::optional<double> cost_usd_per_input_token;
  std::string baseline_key;
  // Reuse a confirmed immutable receipt across a fresh engine. One operation
  // ID cannot authorize different source bytes or additional provider attempts.
  std::string operation_id;
};

std::shared_ptr<resolve::VectorSpace> make_provider_vector_space(
    const ProviderRegistry& registry, const Secrets& secrets, net::HttpTransport& http,
    providers::EmbeddingRequest request, std::shared_ptr<ProviderVectorCache> cache,
    ProviderVectorPolicy policy);
std::shared_ptr<resolve::VectorSpace> make_injected_vector_space(
    std::shared_ptr<EmbeddingProvider> provider, std::shared_ptr<ProviderVectorCache> cache = nullptr,
    std::function<bool()> callable_on_miss = {},
    std::optional<std::string> expected_model = std::nullopt);
std::function<Result<Json>(std::string_view, const Json&)> make_embedding_response_recorder(Runtime& rt);

// execution is context_execution JSON, with an optional embedding section.
// Cached representations may be read offline. Provider misses require both
// explicit settings authorization and a current ContextExecutionScope.
Json install_provider_vector(ContextEngine& engine, Runtime& rt,
    const Json& execution, bool allow_provider_calls = false);

}  // namespace context
}  // namespace loom
