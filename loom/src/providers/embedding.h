// Source-private text embedding adapter. Public ProviderRegistry ABI stays intact.
#pragma once

#include <string>
#include <functional>
#include <optional>
#include <vector>

#include "loom/providers.h"

namespace loom::providers {

struct EmbeddingRequest {
  std::string provider_id;
  std::string model;
  int timeout_ms = 30000;
  Json request_options = Json::object();
  bool calls_authorized = false;
  // Binds a cached-space request to the exact manifest/account it inspected.
  std::string expected_identity;
  // Explicit caller data for routed/aliased model names. Empty means that a
  // reported model must equal the requested model; absence stays unknown.
  std::vector<std::string> accepted_reported_models;
  // Production installs a raw-source recorder. Native callers may supply their
  // own recorder; only sanitized source references are returned or traced.
  std::function<Result<Json>(std::string_view, const Json&)> record_response;
};

struct EmbeddingReply {
  std::vector<std::vector<float>> vectors;
  Json usage = Json::object();
  std::string identity;
  Json response_source = Json::object();
  std::string requested_model;
  std::optional<std::string> reported_model;
};

// The returned identity is an opaque digest, including endpoint, model,
// provider options and credential digest. Neither keys nor headers are exposed.
Result<std::string> embedding_identity(const ProviderRegistry& registry,
    const Secrets& secrets, const EmbeddingRequest& request);
Json embedding_capability(const ProviderRegistry& registry,
    const Secrets& secrets, const EmbeddingRequest& request);

// No credential discovery, routing fallback or retry. The named manifest must
// offer embedding/embed; this adapter supports OpenAI-compatible float JSON.
Result<EmbeddingReply> provider_embed(const ProviderRegistry& registry,
    const Secrets& secrets, net::HttpTransport& http,
    const EmbeddingRequest& request, const std::vector<std::string>& texts,
    bool* request_attempted = nullptr);

}  // namespace loom::providers
