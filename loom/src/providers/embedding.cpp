#include "embedding.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>

#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/util/sha256.h"

namespace loom::providers {
namespace {

Result<ProviderManifest> manifest_for(const ProviderRegistry& registry,
                                    const EmbeddingRequest& request) {
  if (request.provider_id.empty() || request.model.empty() || request.timeout_ms <= 0 ||
      !request.request_options.is_object())
    return Error(Errc::InvalidArgument, "embedding provider, model, positive timeout and object options required");
  if (request.request_options.contains("model") || request.request_options.contains("input"))
    return Error(Errc::InvalidArgument, "embedding options cannot override explicit model or input");
  if (request.request_options.contains("encoding_format") && request.request_options["encoding_format"] != "float")
    return Error(Errc::Unsupported, "embedding adapter supports float encoding only");
  auto manifest = registry.get(request.provider_id);
  if (!manifest) return Error(Errc::Unavailable, "embedding provider not registered");
  bool supported = false;
  for (const auto& capability : manifest->capabilities) {
    if (capability.resource != "embedding" || capability.name != "embed") continue;
    Json constraints = Json::object();
    if (capability.constraints.contains("modalities")) constraints["modalities"] = Json::array({"text"});
    if (capability.constraints.contains("models")) constraints["models"] = request.model;
    if (constraints_satisfied(capability.constraints, constraints)) supported = true;
  }
  if (!supported) return Error(Errc::Unavailable, "embedding text/model capability not offered");
  if (!registry.available(*manifest)) return Error(Errc::Unavailable, "embedding credential unavailable");
  if (manifest->base_url.empty()) return Error(Errc::Unavailable, "embedding endpoint unavailable");
  const auto adapter = json::get_string(manifest->metadata, "embedding_adapter", "openai");
  if (adapter != "openai") return Error(Errc::Unsupported, "embedding adapter not implemented");
  const std::set<std::string> schemes{"none", "bearer", "token", "x-api-key", "query:key"};
  if (!schemes.contains(manifest->auth_scheme))
    return Error(Errc::Unsupported, "embedding authentication scheme not implemented");
  return *manifest;
}

std::string endpoint(const ProviderManifest& manifest) {
  auto base = manifest.base_url;
  while (!base.empty() && base.back() == '/') base.pop_back();
  auto path = json::get_string(manifest.metadata, "embedding_path", "/embeddings");
  if (path.empty() || path.front() != '/') path.insert(path.begin(), '/');
  return base + path;
}

std::string identity_for(const ProviderManifest& manifest, std::string_view credential,
                         const EmbeddingRequest& request) {
  return Sha256::hex(json::canonical(Json{{"provider", manifest.id}, {"endpoint", endpoint(manifest)},
      {"model", request.model}, {"options", request.request_options},
      {"headers", manifest.default_headers}, {"auth_scheme", manifest.auth_scheme},
      {"account_digest", Sha256::hex(credential)}}));
}

}  // namespace

Result<std::string> embedding_identity(const ProviderRegistry& registry,
    const Secrets& secrets, const EmbeddingRequest& request) {
  LOOM_TRY_ASSIGN(auto manifest, manifest_for(registry, request));
  const auto credential = secrets.get_string(manifest.auth_secret);
  if (!manifest.auth_secret.empty() && credential.empty())
    return Error(Errc::Unavailable, "embedding credential unavailable");
  return identity_for(manifest, credential, request);
}

Json embedding_capability(const ProviderRegistry& registry,
    const Secrets& secrets, const EmbeddingRequest& request) {
  auto identity = embedding_identity(registry, secrets, request);
  return Json{{"provider_id", request.provider_id}, {"model", request.model},
      {"available", static_cast<bool>(identity)}, {"modalities", Json::array({"text"})},
      {"calls_authorized", request.calls_authorized},
      {"reason", identity ? "" : std::string(errc_name(identity.error().code))}};
}

Result<EmbeddingReply> provider_embed(const ProviderRegistry& registry,
    const Secrets& secrets, net::HttpTransport& http,
    const EmbeddingRequest& request, const std::vector<std::string>& texts, bool* request_attempted) {
  if (request_attempted) *request_attempted = false;
  try {
    LOOM_TRY_ASSIGN(auto manifest, manifest_for(registry, request));
    const auto key = secrets.get_string(manifest.auth_secret);
    if (!manifest.auth_secret.empty() && key.empty())
      return Error(Errc::Unavailable, "embedding credential unavailable");
    const auto identity = identity_for(manifest, key, request);
    if (!request.expected_identity.empty() && request.expected_identity != identity)
      return Error(Errc::Conflict, "embedding provider identity changed before request");
    if (texts.empty()) return EmbeddingReply{{}, Json::object(), identity, Json::object()};
    if (!request.calls_authorized) return Error(Errc::Unavailable, "embedding calls require explicit authorization");
    Json body = request.request_options;
    body["model"] = request.model;
    body["input"] = texts;
    body["encoding_format"] = "float";
    net::HttpRequest http_request;
    http_request.method = "POST";
    http_request.url = endpoint(manifest);
    http_request.timeout_ms = request.timeout_ms;
    http_request.body = json::dump(body);
    for (auto item = manifest.default_headers.begin(); item != manifest.default_headers.end(); ++item) {
      if (!item->is_string()) return Error(Errc::InvalidArgument, "embedding default header must be text");
      http_request.headers.emplace_back(item.key(), item->get<std::string>());
    }
    http_request.headers.emplace_back("Content-Type", "application/json");
    if (manifest.auth_scheme == "bearer") http_request.headers.emplace_back("Authorization", "Bearer " + key);
    else if (manifest.auth_scheme == "token") http_request.headers.emplace_back("Authorization", "token " + key);
    else if (manifest.auth_scheme == "x-api-key") http_request.headers.emplace_back("x-api-key", key);
    else if (manifest.auth_scheme == "query:key") http_request.url = net::with_query(http_request.url, {{"key", key}});
    if (request_attempted) *request_attempted = true;
    std::string received_body;
    bool received_headers = false;
    int received_status = 0;
    net::StreamSink sink;
    sink.on_headers = [&](int status, const net::Headers&) {
      received_headers = true;
      received_status = status;
      return true;
    };
    sink.on_data = [&](std::string_view chunk) {
      received_body.append(chunk);
      return true;
    };
    Result<net::HttpResponse> response = Error(Errc::Network, "embedding transport failed");
    try { response = http.send(http_request, request.record_response ? &sink : nullptr); }
    catch (...) { response = Error(Errc::Network, "embedding transport failed"); }
    if (response) {
      received_status = response->status;
      // Honor buffered native transports too; the HTTP contract says compliant
      // streaming transports leave response.body empty when delivering chunks.
      if (received_body.empty()) received_body = response->body;
      response->body = received_body;
    }
    Json source = Json::object();
    if (request.record_response && (received_headers || response || !received_body.empty())) {
      Json input_hashes = Json::array();
      for (const auto& text : texts) input_hashes.push_back(Sha256::hex(text));
      auto recorded = request.record_response(received_body, Json{{"provider_id", request.provider_id},
          {"model", request.model}, {"status", received_status}, {"complete", static_cast<bool>(response)},
          {"identity", identity}, {"input_hashes", input_hashes}, {"request_sha256", Sha256::hex(http_request.body)},
          {"transport_error", response ? Json(nullptr) : Json(std::string(errc_name(response.error().code)))}});
      if (!recorded) return Error(recorded.error().code, "embedding first-response storage failed");
      source = *recorded;
    }
    if (!response) return Error(response.error().code, "embedding transport failed");
    if (!response->ok()) return Error(Errc::Http, "embedding HTTP request failed with status " + std::to_string(response->status));
    auto parsed = response->json();
    if (!parsed || !parsed->is_object()) return Error(Errc::Parse, "embedding response is not a JSON object");
    const auto* data = json::find(*parsed, "data");
    if (!data || !data->is_array() || data->size() != texts.size())
      return Error(Errc::Parse, "embedding response count mismatch");
    EmbeddingReply reply;
    reply.identity = identity;
    reply.response_source = std::move(source);
    reply.vectors.resize(texts.size());
    std::set<std::size_t> seen;
    std::optional<std::size_t> dimensions;
    for (const auto& row : *data) {
      const auto* index = json::find(row, "index");
      const auto* vector = json::find(row, "embedding");
      if (!index || !index->is_number_integer() ||
          (!index->is_number_unsigned() && index->get<std::int64_t>() < 0) ||
          index->get<std::uint64_t>() >= texts.size() || !vector || !vector->is_array() || vector->empty())
        return Error(Errc::Parse, "embedding response index/vector invalid");
      const auto position = index->get<std::size_t>();
      if (!seen.insert(position).second) return Error(Errc::Parse, "embedding response duplicate index");
      if (dimensions && *dimensions != vector->size()) return Error(Errc::Parse, "embedding vector dimension mismatch");
      dimensions = vector->size();
      for (const auto& value : *vector) {
        if (!value.is_number()) return Error(Errc::Parse, "embedding vector contains nonnumber");
        const double number = value.get<double>();
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max() ||
            (number != 0 && static_cast<float>(number) == 0))
          return Error(Errc::Parse, "embedding vector contains unrepresentable number");
        reply.vectors[position].push_back(static_cast<float>(number));
      }
    }
    if (const auto* usage = json::find(*parsed, "usage"); usage && usage->is_object()) reply.usage = *usage;
    return reply;
  } catch (const std::exception&) {
    return Error(Errc::Parse, "embedding adapter failed");
  } catch (...) {
    return Error(Errc::Internal, "embedding adapter failed");
  }
}

}  // namespace loom::providers
