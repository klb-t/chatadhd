// Source-private graph reply execution adapter. The model format/compiler and
// method definitions remain external versioned data and W4 operations.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "loom/net/http.h"
#include "loom/util/json.h"

namespace loom {
class BlobStore;
class Config;
class Database;
class ProviderRegistry;
class ProvenanceStore;
class Secrets;

namespace chat {

// Borrowed synchronous services. No Runtime singleton, second database or
// retained pointer map is introduced by this adapter.
struct GraphReplyServices {
  std::filesystem::path root;
  Database& db;
  const Config& config;
  const Secrets& secrets;
  net::HttpTransport& http;
  ProviderRegistry* providers;
  BlobStore& blobs;
  ProvenanceStore& provenance;
};

struct GraphReplyCallbacks {
  // Defaults to real packet::execute when W4 is available at compilation.
  std::function<Result<Json>(const Json&)> operation;
  // Result: {packet:<bound candidate>,manifest:<completed method trace>}.
  std::function<Result<Json>(const Json& candidate_packet, const Json& manifest,
                            const Json& result_bindings)> bind_results;
  // The existing GraphPacketStore's accept/read/replay interface.
  std::function<Result<Json>(const Json& store_request)> accept;
};

struct GraphReplyPrepared {
  std::string mode;
  Json options;
  Json base_packet;
  Json host;
  Json effective_recipe;
  Json manifest;
  Json request_patch;
  Json append_messages;
  Json capability;
  // Assigned by the caller after final request merging. Structural method
  // Claims use the actual host instrument origin, independent of model content.
  Json binding_origin = nullptr;
  std::string request_bytes;
  GraphReplyCallbacks callbacks;
};

// recipe.request is fully instantiated by MethodRegistry request_bindings.
// Prompt bytes, response formats and output bindings are recipe data, never
// mode-specific C++ presets. A missing dependency/definition is explicit.
Result<GraphReplyPrepared> prepare_graph_reply(const Json& options, const Json& base_packet,
    const Json& host, const Json& effective_recipe, const Json& manifest,
    GraphReplyCallbacks callbacks = {});

Result<Json> graph_reply_packet_operation(const Json& command);

// Exact bytes are persisted before parsing. The return value contains source
// references, hashes and instrumentation only, never transport credentials.
Result<Json> retain_graph_reply_bytes(GraphReplyServices& services, std::string_view bytes,
                                     const Json& metadata);

// Every ordinary failure is represented in a value retaining the original
// display text and first-response reference. The compilation is never edited.
Json finish_graph_reply(GraphReplyServices& services, const GraphReplyPrepared& prepared,
    std::string_view model_content, std::string_view retained_display_text,
    const Json& first_response_ref, const Json& instrumentation = Json::object());

// Uses original base + compilation through W4's verified fragment operation.
Result<Json> graph_reply_fragment(const Json& reply_result, const Json& address,
                                 const GraphReplyCallbacks& callbacks = {});

// Shared single-attempt guard, usable around the main graph-generation call
// and the separate postprocessor. Expected resources come from caller data;
// configured output caps are never substituted for expected consumption.
class ModelUsageGuard {
 public:
  explicit ModelUsageGuard(GraphReplyServices& services);
  ~ModelUsageGuard();
  ModelUsageGuard(const ModelUsageGuard&) = delete;
  ModelUsageGuard& operator=(const ModelUsageGuard&) = delete;
  Result<Json> admit(const net::HttpRequest& request, const Json& estimate,
                     const Json& confirmation = Json::object());
  Result<Json> settle(bool request_attempted, std::size_t received_bytes,
                      const Json& provider_usage = Json::object());
  const Json& decisions() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Named ProviderRegistry llm/chat.completions manifest; exactly one explicit
// authorized attempt, no routing fallback/repair/retry. payload is the actual
// fully bound recipe request. HTTP errors and partial first responses retain
// the primary text. transport_options contains provider_id, timeout_ms,
// calls_authorized and caller usage_estimate/confirmation, all data.
Json run_graph_reply_postprocess(GraphReplyServices& services, const GraphReplyPrepared& prepared,
    const Json& payload, const Json& transport_options, std::string_view retained_display_text,
    const Json& primary_response_ref, const CancelToken* cancel = nullptr);

}  // namespace chat
}  // namespace loom
