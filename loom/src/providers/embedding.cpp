#include "embedding.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>

#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/use_model.h"
#include "loom/util/sha256.h"

namespace loom::providers {
namespace {

// The adapter family the manifest names for embeddings (data); "openai" is the
// shorthand of the OpenAI-compatible wire shape.
std::string adapter_family(const ProviderManifest& manifest) {
  const auto named = json::get_string(manifest.metadata, "embedding_adapter", "openai");
  return named == "openai" ? "openai_embeddings" : named;
}

Result<ProviderManifest> manifest_for(const ProviderRegistry& registry,
                                    const EmbeddingRequest& request) {
  if (request.provider_id.empty() || request.model.empty() || request.timeout_ms <= 0 ||
      !request.request_options.is_object())
    return Error(Errc::InvalidArgument, "embedding provider, model, positive timeout and object options required");
  if (request.request_options.contains("model") || request.request_options.contains("input"))
    return Error(Errc::InvalidArgument, "embedding options cannot override explicit model or input");
  if (request.request_options.contains("encoding_format") && request.request_options["encoding_format"] != "float")
    return Error(Errc::Unsupported, "embedding adapter supports float encoding only");
  for (const auto& model : request.accepted_reported_models)
    if (model.empty()) return Error(Errc::InvalidArgument, "embedding model aliases must be nonempty strings");
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
  const auto adapter = llm::make_adapter(adapter_family(*manifest));
  const auto produces = adapter ? adapter->capabilities().produces : std::vector<std::string>{};
  if (std::find(produces.begin(), produces.end(), "embedding") == produces.end())
    return Error(Errc::Unsupported, "embedding adapter not implemented");
  const std::set<std::string> schemes{"none", "bearer", "token", "x-api-key", "query:key"};
  if (!schemes.contains(manifest->auth_scheme))
    return Error(Errc::Unsupported, "embedding authentication scheme not implemented");
  return *manifest;
}

std::string endpoint(const ProviderManifest& manifest) {
  return llm::embedding_url(manifest.base_url, manifest.metadata);
}

std::string identity_for(const ProviderManifest& manifest, std::string_view credential,
                         const EmbeddingRequest& request) {
  return Sha256::hex(json::canonical(Json{{"provider", manifest.id}, {"endpoint", endpoint(manifest)},
      {"model", request.model}, {"options", request.request_options},
      {"accepted_reported_models", request.accepted_reported_models},
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
      {"requested_model", request.model}, {"reported_model", nullptr},
      {"available", static_cast<bool>(identity)}, {"modalities", Json::array({"text"})},
      {"readiness", identity ? "manifest_credential_adapter" : "unavailable"},
      {"live_verification", "not_performed"}, {"model_attestation", "unknown"},
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
    if (texts.empty()) {
      EmbeddingReply reply;
      reply.identity = identity;
      reply.requested_model = request.model;
      return reply;
    }
    if (!request.calls_authorized) return Error(Errc::Unavailable, "embedding calls require explicit authorization");
    Json body = request.request_options;
    body["model"] = request.model;
    body["input"] = texts;
    body["encoding_format"] = "float";

    // One use-model call; the vector policy admitted the operation already.
    llm::Services services(registry.config(), secrets, http);
    services.providers = &registry;
    llm::ModelRequest mreq;
    mreq.kind = "embedding.embed";
    mreq.action = "embedding.embed";
    mreq.models = {llm::ModelSpec{request.model, "", Json::object()}};
    mreq.body = body;  // the complete payload, options first, sent byte for byte
    for (const auto& text : texts) {
      llm::Part part;
      part.kind = "text";
      part.text = text;
      mreq.inputs.push_back(std::move(part));
    }
    mreq.wants = {"embedding"};
    mreq.route.provider = manifest.id;
    mreq.route.base_url = manifest.base_url;
    mreq.route.adapter = adapter_family(manifest);
    mreq.referer = false;
    mreq.timeout_ms = request.timeout_ms;
    mreq.capture = true;  // the exact first response is recorded before it is judged
    mreq.contain_exceptions = Error(Errc::Network, "embedding transport failed");
    mreq.usage.gate = llm::make_admitted_gate();
    mreq.hooks.on_dispatch = [&] { if (request_attempted) *request_attempted = true; };
    auto call = llm::prepare_call(services, mreq);
    if (!call) return Error(call.error().code, call.error().message);
    const auto sent = llm::execute_call(services, mreq, *call);

    const bool transport_ok = !sent.error.has_value();
    Json source = Json::object();
    if (request.record_response && sent.sent && (sent.http_status != 0 || transport_ok || !sent.body.empty())) {
      // This optional inspection never rejects bytes before immutable capture.
      // It records a provider report, not verification of the requested model.
      Json reported_model = nullptr;
      auto metadata_body = json::parse(sent.body);
      if (metadata_body && metadata_body->is_object()) {
        const auto* reported = json::find(*metadata_body, "model");
        if (reported && reported->is_string() && !reported->get_ref<const std::string&>().empty())
          reported_model = *reported;
      }
      Json input_hashes = Json::array();
      for (const auto& text : texts) input_hashes.push_back(Sha256::hex(text));
      auto recorded = request.record_response(sent.body, Json{{"provider_id", request.provider_id},
          {"model", request.model}, {"requested_model", request.model}, {"reported_model", reported_model},
          {"status", sent.http_status}, {"complete", transport_ok},
          {"identity", identity}, {"input_hashes", input_hashes}, {"request_sha256", Sha256::hex(call->request.body)},
          {"transport_error", transport_ok ? Json(nullptr) : Json(std::string(errc_name(sent.error->code)))}});
      if (!recorded) return Error(recorded.error().code, "embedding first-response storage failed");
      source = *recorded;
    }
    if (!transport_ok) return Error(sent.error->code, "embedding transport failed");
    if (sent.state == llm::State::HttpError)
      return Error(Errc::Http, "embedding HTTP request failed with status " + std::to_string(sent.http_status));
    if (sent.parse_error) return *sent.parse_error;
    if (sent.state != llm::State::Ok) return sent.error ? *sent.error : Error(Errc::Parse, "embedding response invalid");
    std::vector<const llm::Part*> rows;
    for (const auto& part : sent.outputs)
      if (part.kind == "embedding") rows.push_back(&part);
    if (rows.size() != texts.size()) return Error(Errc::Parse, "embedding response count mismatch");
    EmbeddingReply reply;
    reply.identity = identity;
    reply.requested_model = request.model;
    if (!sent.reported_model.empty()) {
      reply.reported_model = sent.reported_model;
      if (*reply.reported_model != request.model &&
          std::find(request.accepted_reported_models.begin(), request.accepted_reported_models.end(),
                    *reply.reported_model) == request.accepted_reported_models.end())
        return Error(Errc::Conflict, "embedding reported model does not match requested binding");
    }
    reply.response_source = std::move(source);
    reply.vectors.resize(texts.size());
    for (std::size_t position = 0; position < rows.size(); ++position) {
      for (const auto& value : rows[position]->value) {
        const double number = value.get<double>();
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max() ||
            (number != 0 && static_cast<float>(number) == 0))
          return Error(Errc::Parse, "embedding vector contains unrepresentable number");
        reply.vectors[position].push_back(static_cast<float>(number));
      }
    }
    reply.usage = sent.usage;
    return reply;
  } catch (const std::exception&) {
    return Error(Errc::Parse, "embedding adapter failed");
  } catch (...) {
    return Error(Errc::Internal, "embedding adapter failed");
  }
}

}  // namespace loom::providers
