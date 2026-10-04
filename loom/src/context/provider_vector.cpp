#include "provider_vector.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>

#include "loom/context_engine.h"
#include "loom/runtime.h"
#include "loom/selector.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"

#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#define LOOM_VECTOR_USAGE_POLICY 1
#endif
#if __has_include("context_execution.h")
#include "context_execution.h"
#define LOOM_VECTOR_EXECUTION_SCOPE 1
#endif
#if __has_include("context_usage_claim.h")
#include "context_usage_claim.h"
#define LOOM_VECTOR_USAGE_CLAIM 1
#endif

namespace loom::context {
namespace {

std::string cache_key(std::string_view identity, std::string_view hash) {
  return Sha256::hex(json::canonical(Json::array({identity, hash})));
}

Status valid_vectors(const std::vector<std::vector<float>>& vectors, std::size_t expected) {
  if (vectors.size() != expected) return Error(Errc::Parse, "embedding vector count mismatch");
  std::optional<std::size_t> dimensions;
  for (const auto& vector : vectors) {
    if (vector.empty()) return Error(Errc::Parse, "embedding vector missing dimensions");
    if (dimensions && *dimensions != vector.size()) return Error(Errc::Parse, "embedding dimensions inconsistent");
    dimensions = vector.size();
    for (float value : vector) if (!std::isfinite(value)) return Error(Errc::Parse, "embedding vector not finite");
  }
  return {};
}

resolve::SparseVec sparse(const std::vector<float>& vector) {
  resolve::SparseVec out;
  // Preserve direction without overflow/underflow in cosine norm calculation.
  double scale = 0;
  for (float value : vector) scale = std::max(scale, std::abs(static_cast<double>(value)));
  if (scale == 0) return out;
  for (std::size_t i = 0; i < vector.size(); ++i)
    if (vector[i] != 0) out["#" + std::to_string(i)] = static_cast<double>(vector[i]) / scale;
  return out;
}

// The weak owner key isolates swapped providers that reuse the same model name.
// No Runtime pointer/lifetime is retained. Expired entries are pruned on use.
struct InjectedIdentity {
  std::string nonce;
  std::shared_ptr<ProviderVectorCache> cache;
};
InjectedIdentity injected_identity(const std::shared_ptr<EmbeddingProvider>& provider,
                                  std::shared_ptr<ProviderVectorCache> supplied) {
  static std::mutex mutex;
  static std::map<std::weak_ptr<EmbeddingProvider>, InjectedIdentity,
                  std::owner_less<std::weak_ptr<EmbeddingProvider>>> identities;
  std::lock_guard lock(mutex);
  for (auto it = identities.begin(); it != identities.end();) {
    if (it->first.expired()) it = identities.erase(it); else ++it;
  }
  const std::weak_ptr<EmbeddingProvider> weak = provider;
  auto [it, inserted] = identities.try_emplace(weak);
  if (inserted) it->second = {random_hex(32), supplied ? supplied : std::make_shared<ProviderVectorCache>()};
  return {it->second.nonce, supplied ? supplied : it->second.cache};
}

class CachedVectorSpace final : public resolve::VectorSpace {
 public:
  using Identity = std::function<Result<std::string>()>;
  using Embed = std::function<Result<std::vector<std::vector<float>>>(
      const std::string&, const std::vector<std::string>&)>;
  CachedVectorSpace(std::string method, Identity identity, Embed embed, std::shared_ptr<ProviderVectorCache> cache)
      : method_(std::move(method)), identity_(std::move(identity)), embed_(std::move(embed)),
        cache_(cache ? std::move(cache) : std::make_shared<ProviderVectorCache>()) {}
  std::string method() const override { return method_; }
  std::vector<std::string> modalities() const override { return {"text"}; }
  Status fit(const std::vector<resolve::EmbedInput>&) override { return {}; }
  Result<std::vector<resolve::SparseVec>> vectors(const std::vector<resolve::EmbedInput>& inputs) override {
    std::lock_guard lock(mutex_);
    try {
      LOOM_TRY_ASSIGN(auto identity, identity_());
      std::vector<std::vector<float>> dense(inputs.size());
      std::map<std::string, std::size_t> positions;
      std::vector<std::string> missing, hashes;
      for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i].modality != "text" || inputs[i].text.empty()) continue;
        // A supplied hash is provenance only. Cache identity is recomputed from
        // exact bytes; a reused/mistaken hash cannot alias changed source text.
        const auto hash = Sha256::hex(inputs[i].text);
        LOOM_TRY_ASSIGN(auto cached, cache_->get(identity, hash));
        if (cached) dense[i] = std::move(*cached);
        else if (positions.try_emplace(hash, missing.size()).second) {
          missing.push_back(inputs[i].text);
          hashes.push_back(hash);
        }
      }
      if (!missing.empty()) {
        LOOM_TRY_ASSIGN(auto values, embed_(identity, missing));
        LOOM_TRY(valid_vectors(values, missing.size()));
        // Check against existing cache dimensions before writing any fresh
        // value: a changed provider representation must not be silently mixed.
        std::optional<std::size_t> dimensions;
        for (const auto& vector : dense) if (!vector.empty()) {
          if (dimensions && *dimensions != vector.size()) return Error(Errc::Parse, "cached embedding dimensions inconsistent");
          dimensions = vector.size();
        }
        if (dimensions && *dimensions != values.front().size())
          return Error(Errc::Parse, "provider embedding dimensions changed");
        for (std::size_t i = 0; i < hashes.size(); ++i) LOOM_TRY(cache_->put(identity, hashes[i], values[i]));
        for (std::size_t i = 0; i < inputs.size(); ++i) {
          if (!dense[i].empty() || inputs[i].modality != "text" || inputs[i].text.empty()) continue;
          dense[i] = values[positions.at(Sha256::hex(inputs[i].text))];
        }
      }
      std::optional<std::size_t> dimensions;
      std::vector<resolve::SparseVec> out;
      out.reserve(inputs.size());
      for (const auto& vector : dense) {
        if (!vector.empty()) {
          if (dimensions && *dimensions != vector.size()) return Error(Errc::Parse, "cached embedding dimensions inconsistent");
          dimensions = vector.size();
        }
        out.push_back(sparse(vector));
      }
      return out;
    } catch (const std::exception&) { return Error(Errc::Internal, "embedding instrument exception"); }
    catch (...) { return Error(Errc::Internal, "embedding instrument exception"); }
  }

 private:
  std::string method_;
  Identity identity_;
  Embed embed_;
  std::shared_ptr<ProviderVectorCache> cache_;
  std::mutex mutex_;
};

Json reported_quantity(const Json& usage, std::initializer_list<const char*> keys) {
  for (auto key : keys) {
    const auto* value = json::find(usage, key);
    if (value && value->is_number() && std::isfinite(value->get<double>()) && value->get<double>() >= 0) return *value;
  }
  return nullptr;
}

bool bool_setting(const Json& section, const char* key, bool fallback) {
  auto* value = json::find(section, key);
  return value && value->is_boolean() ? value->get<bool>() : fallback;
}

}  // namespace

ProviderVectorCache::ProviderVectorCache(std::filesystem::path directory) : directory_(std::move(directory)) {}

Result<std::optional<std::vector<float>>> ProviderVectorCache::get(std::string_view identity, std::string_view hash) {
  std::lock_guard lock(mutex_);
  const auto key = cache_key(identity, hash);
  auto existing = vectors_.find(key);
  if (existing != vectors_.end()) return std::optional<std::vector<float>>(existing->second);
  if (directory_.empty()) return std::optional<std::vector<float>>{};
  const auto path = directory_ / (key + ".json");
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec) return Error(Errc::Io, "embedding cache lookup failed");
  if (!exists) return std::optional<std::vector<float>>{};
  LOOM_TRY_ASSIGN(auto text, fsutil::read_file(path));
  auto parsed = json::parse(text);
  if (!parsed || !parsed->is_array()) return Error(Errc::Parse, "embedding cache invalid");
  std::vector<float> vector;
  for (const auto& value : *parsed) {
    if (!value.is_number() || !std::isfinite(value.get<double>()) ||
        std::abs(value.get<double>()) > std::numeric_limits<float>::max() ||
        (value.get<double>() != 0 && value.get<float>() == 0))
      return Error(Errc::Parse, "embedding cache vector invalid");
    vector.push_back(value.get<float>());
  }
  LOOM_TRY(valid_vectors({vector}, 1));
  vectors_[key] = vector;
  return std::optional<std::vector<float>>(std::move(vector));
}

Status ProviderVectorCache::put(std::string_view identity, std::string_view hash, const std::vector<float>& vector) {
  LOOM_TRY(valid_vectors({vector}, 1));
  std::lock_guard lock(mutex_);
  const auto key = cache_key(identity, hash);
  if (!directory_.empty()) {
    // Different engines/processes share this derived cache. Unique temporary
    // files avoid atomic_write's fixed .tmp name racing for the same key.
    const auto temporary = directory_ / (key + "." + random_hex(32) + ".json");
    auto written = fsutil::atomic_write(temporary, json::dump(Json(vector)), {.owner_only = true});
    if (!written) return written.error();
    std::error_code error;
    std::filesystem::rename(temporary, directory_ / (key + ".json"), error);
    if (error) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return Error(Errc::Io, "embedding cache publication failed");
    }
  }
  vectors_[key] = vector;
  return {};
}

std::size_t ProviderVectorCache::size() const { std::lock_guard lock(mutex_); return vectors_.size(); }

std::shared_ptr<resolve::VectorSpace> make_provider_vector_space(
    const ProviderRegistry& registry, const Secrets& secrets, net::HttpTransport& http,
    providers::EmbeddingRequest request, std::shared_ptr<ProviderVectorCache> cache, ProviderVectorPolicy policy) {
  const auto method = "embedding:" + request.provider_id + ":" + request.model;
  auto identity = [&registry, &secrets, request] { return providers::embedding_identity(registry, secrets, request); };
  auto embed = [&registry, &secrets, &http, request, policy, pending = std::map<std::string, std::string>{}](const std::string& expected,
      const std::vector<std::string>& texts) mutable -> Result<std::vector<std::vector<float>>> {
    if (!request.calls_authorized || !policy.admit || !policy.complete)
      return Error(Errc::Unavailable, "embedding admission policy/authorization missing");
    if (!policy.estimated_resources.is_object()) return Error(Errc::InvalidArgument, "embedding estimate must be an object");
    double bytes = 0;
    for (const auto& text : texts) bytes += static_cast<double>(text.size());
    Json resources = policy.estimated_resources;
    resources["requests"] = 1;
    resources["input_bytes"] = bytes;
    resources["input_items"] = texts.size();
    if (policy.input_tokens_per_byte) {
      if (!std::isfinite(*policy.input_tokens_per_byte) || *policy.input_tokens_per_byte < 0)
        return Error(Errc::InvalidArgument, "embedding token estimate rate invalid");
      resources["input_tokens"] = bytes * *policy.input_tokens_per_byte;
    }
    if (policy.cost_usd_per_input_token) {
      if (!std::isfinite(*policy.cost_usd_per_input_token) || *policy.cost_usd_per_input_token < 0)
        return Error(Errc::InvalidArgument, "embedding price estimate rate invalid");
      auto tokens = reported_quantity(resources, {"input_tokens"});
      resources["cost_usd"] = tokens.is_null() ? Json(nullptr) : Json(tokens.get<double>() * *policy.cost_usd_per_input_token);
    }
    const auto baseline = policy.baseline_key.empty() ? "context.embedding:" + expected : policy.baseline_key;
    Json hashes = Json::array();
    for (const auto& text : texts) hashes.push_back(Sha256::hex(text));
    Json estimate{{"baseline_key", baseline},
        {"resources", resources}, {"provider_id", request.provider_id}, {"model", request.model},
        {"input_hashes", hashes}, {"estimate_provenance", "declared"}};
    const auto fingerprint = Sha256::hex(json::canonical(estimate));
    auto operation = policy.operation_id;
    if (operation.empty()) {
      auto [it, inserted] = pending.try_emplace(fingerprint);
      if (inserted) it->second = "embedding_" + random_hex(32);
      operation = it->second;
    }
    estimate["operation_id"] = operation;
    LOOM_TRY_ASSIGN(auto admission, policy.admit(estimate));
    if (json::get_string(admission, "status") != "allowed")
      return Error(Errc::Paused, "embedding admission pending: " + json::dump(admission));
    policy.operation_id.clear();  // supplied resume receipt authorizes this batch once
    request.expected_identity = expected;
    pending.erase(fingerprint);  // admission is consumed by this single attempt
    bool attempted = false;
    auto reply = providers::provider_embed(registry, secrets, http, request, texts, &attempted);
    if (!attempted && !reply && policy.cancel) {
      auto cancelled = policy.cancel(operation, "embedding_request_not_sent");
      if (!cancelled) return Error(cancelled.error().code, "embedding cancellation failed before request");
      return reply.error();
    }
    Json actual{{"resources", Json{{"requests", 1}, {"input_bytes", bytes}, {"input_items", texts.size()},
        {"input_tokens", nullptr}, {"cost_usd", nullptr}}}, {"provenance", "instrument_measured"}};
    if (!attempted) {
      // No transport invocation occurred. These zeros are known, not unknown
      // charge estimates; avoid training consumption baselines on a preflight.
      actual["resources"] = Json{{"requests", 0}, {"input_bytes", 0}, {"input_items", 0},
          {"input_tokens", 0}, {"cost_usd", 0}};
      actual["provenance"] = "declared";
    }
    if (reply) {
      actual["resources"]["input_tokens"] = reported_quantity(reply->usage, {"prompt_tokens", "input_tokens", "total_tokens"});
      actual["resources"]["cost_usd"] = reported_quantity(reply->usage, {"cost", "cost_usd"});
      actual["provenance"] = "provider_reported";
      if (!reply->response_source.empty()) actual["response_source"] = reply->response_source;
    }
    // Even a timeout/malformed response may incur a charge. Unknown quantities
    // retain their ledger reservation; cancellation never invents free usage.
    auto accounted = policy.complete(operation, actual);
    if (!accounted) return Error(accounted.error().code, "embedding accounting failed after attempted request");
    if (!reply) return reply.error();
    return std::move(reply->vectors);
  };
  return std::make_shared<CachedVectorSpace>(method, std::move(identity), std::move(embed), std::move(cache));
}

std::shared_ptr<resolve::VectorSpace> make_injected_vector_space(
    std::shared_ptr<EmbeddingProvider> provider, std::shared_ptr<ProviderVectorCache> cache,
    std::function<bool()> callable_on_miss) {
  if (!provider) return nullptr;
  auto shared = injected_identity(provider, std::move(cache));
  auto identity = [provider, nonce = shared.nonce]() -> Result<std::string> {
    return Sha256::hex(json::canonical(Json::array({"injected", nonce, provider->model_id()})));
  };
  auto embed = [provider, callable_on_miss](const std::string&, const std::vector<std::string>& texts)
      -> Result<std::vector<std::vector<float>>> {
    if (callable_on_miss && !callable_on_miss())
      return Error(Errc::Unavailable, "injected embedding execution scope inactive");
    return provider->embed(texts);
  };
  return std::make_shared<CachedVectorSpace>("embedding:" + provider->model_id(), std::move(identity), std::move(embed), shared.cache);
}

std::function<Result<Json>(std::string_view, const Json&)> make_embedding_response_recorder(Runtime& rt) {
  return [&rt](std::string_view bytes, const Json& metadata) -> Result<Json> {
    Json references{{"schema", "loom.embedding_response/1"}, {"bytes", bytes.size()},
        {"status", metadata.value("status", Json(0))}, {"complete", metadata.value("complete", false)},
        {"provider_id", json::get_string(metadata, "provider_id")}, {"model", json::get_string(metadata, "model")}};
    auto record_trace = [&](const Json& trace) {
#if defined(LOOM_VECTOR_EXECUTION_SCOPE)
      if (auto* scope = current_context_execution_scope()) scope->record_usage_decision(trace);
#else
      (void)trace;
#endif
    };
    auto blob = rt.blobs().put(bytes, "application/json");
    if (!blob) {
      references["source_status"] = "unavailable";
      references["storage_error"] = std::string(errc_name(blob.error().code));
      record_trace(references);
      return Error(blob.error().code, "embedding response blob storage failed");
    }
    references["blob_hash"] = blob->hash;
    SourceRecord source;
    source.kind = "api";
    source.blob_hash = blob->hash;
    source.size = blob->size;
    source.mime = "application/json";
    source.format = "json";
    source.title = "Embedding first response";
    source.parser = "loom.context.embedding";
    source.parser_version = "1";
    source.metadata = metadata;
    auto source_id = rt.provenance().add_source(std::move(source));
    if (!source_id) {
      references["source_status"] = "unavailable";
      references["storage_error"] = std::string(errc_name(source_id.error().code));
      record_trace(references);
      return Error(source_id.error().code, "embedding response source storage failed");
    }
    references["source_id"] = *source_id;
    references["source_status"] = "recorded";
    record_trace(references);
    return references;
  };
}

Json install_provider_vector(ContextEngine& engine, Runtime& rt, const Json& execution, bool allow_provider_calls) {
  try {
    const auto* settings = json::find(execution, "embedding");
    const Json section = settings && settings->is_object() ? *settings : Json::object();
    const auto channel_id = json::get_string(section, "channel_id", "vector");
    Json status{{"id", channel_id}, {"available", false}, {"status", "unavailable"}, {"offline_cache", true}};
    if (channel_id.empty()) { status["reason"] = "embedding_channel_id_empty"; return status; }
    if (settings && !settings->is_object()) { status["reason"] = "embedding_options_invalid"; return status; }
    for (const auto* key : {"enabled", "calls_authorized"}) {
      const auto* value = json::find(section, key);
      if (value && !value->is_boolean()) { status["reason"] = "embedding_boolean_option_invalid"; return status; }
    }
    if (settings && !bool_setting(section, "enabled", false)) { status["reason"] = "embedding_disabled"; return status; }
    if (auto injected = rt.embedding_provider()) {
      // Constructor supplies the native capability once. Absent scoped options
      // do not replace an explicit externally installed candidate channel later.
      status["model"] = injected->model_id();
      if (!allow_provider_calls || settings) {
        auto callable = [allow_provider_calls, section] {
#if defined(LOOM_VECTOR_EXECUTION_SCOPE)
          const auto* scope = current_context_execution_scope();
          const auto* current = scope ? json::find(scope->options(), "embedding") : nullptr;
          return allow_provider_calls && current && *current == section && bool_setting(*current, "enabled", false) &&
              bool_setting(*current, "calls_authorized", false);
#else
          (void)allow_provider_calls;
          (void)section;
          return false;
#endif
        };
        engine.set_candidate_channel(channel_id, make_vector_candidate_channel(
            make_injected_vector_space(std::move(injected), nullptr, std::move(callable))));
      }
      status["available"] = true;
      status["status"] = "available";
      status["source"] = "native_injection";
      status["calls_authorized"] = allow_provider_calls && settings && bool_setting(section, "calls_authorized", false);
      return status;
    }
    if (!settings) { status["reason"] = "embedding_not_configured"; return status; }
    providers::EmbeddingRequest request;
    request.provider_id = json::get_string(section, "provider_id");
    request.model = json::get_string(section, "model");
    request.calls_authorized = allow_provider_calls && bool_setting(section, "calls_authorized", false);
    request.record_response = make_embedding_response_recorder(rt);
    if (const auto* timeout = json::find(section, "timeout_ms")) {
      if (!timeout->is_number_integer() || (timeout->is_number_unsigned() && timeout->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) ||
          timeout->get<std::int64_t>() <= 0 || timeout->get<std::int64_t>() > std::numeric_limits<int>::max()) {
        status["reason"] = "embedding_timeout_invalid"; return status;
      }
      request.timeout_ms = timeout->get<int>();
    }
    if (section.contains("request_options")) request.request_options = section["request_options"];
    status.update(providers::embedding_capability(rt.providers(), rt.secrets(), request));
    if (!status["available"].get<bool>()) return status;
    ProviderVectorPolicy policy;
    const auto* usage_settings = json::find(section, "usage");
    if (usage_settings && !usage_settings->is_object()) {
      status["available"] = false; status["reason"] = "embedding_usage_options_invalid"; return status;
    }
    const Json usage = usage_settings ? *usage_settings : Json::object();
    if (section.contains("estimated_resources")) policy.estimated_resources = section["estimated_resources"];
    policy.baseline_key = json::get_string(usage, "baseline_key", json::get_string(section, "baseline_key"));
    policy.operation_id = json::get_string(usage, "resume_operation_id",
        json::get_string(usage, "operation_id", json::get_string(section, "operation_id")));
    if (const auto* confirmation = json::find(usage, "confirmation")) {
      if (!confirmation->is_object() || !confirmation->contains("approved") || !(*confirmation)["approved"].is_boolean() ||
          json::get_string(*confirmation, "receipt_id").empty() || json::get_string(*confirmation, "ref").empty() || policy.operation_id.empty()) {
        status["available"] = false; status["reason"] = "embedding_confirmation_invalid"; return status;
      }
    }
    for (const auto* rate : {"input_tokens_per_byte", "cost_usd_per_input_token"}) {
      const auto* value = json::find(section, rate);
      if (!value || value->is_null()) continue;
      if (!value->is_number() || !std::isfinite(value->get<double>()) || value->get<double>() < 0) {
        status["available"] = false; status["reason"] = "embedding_estimate_rate_invalid"; return status;
      }
      if (std::string_view(rate) == "input_tokens_per_byte") policy.input_tokens_per_byte = value->get<double>();
      else policy.cost_usd_per_input_token = value->get<double>();
    }
#if defined(LOOM_VECTOR_USAGE_POLICY) && defined(LOOM_VECTOR_EXECUTION_SCOPE)
    if (request.calls_authorized) {
      auto policy_options = effective_usage_policy_options(rt.config());
      if (!policy_options) { status["available"] = false; status["reason"] = "usage_policy_options_invalid"; return status; }
      auto ledger = UsagePolicy::open(rt.paths().root / "usage-policy.sqlite", *policy_options);
      if (!ledger) { status["available"] = false; status["reason"] = "usage_policy_unavailable"; return status; }
      std::shared_ptr<UsagePolicy> shared(std::move(*ledger));
      policy.admit = [shared, section, usage, data_root = rt.paths().root,
                      confirmation_operation = policy.operation_id](const Json& estimate) -> Result<Json> {
        const auto* scope = current_context_execution_scope();
        const auto* current = scope ? json::find(scope->options(), "embedding") : nullptr;
        if (!current || *current != section || !bool_setting(*current, "enabled", false) ||
            !bool_setting(*current, "calls_authorized", false))
          return Error(Errc::Unavailable, "embedding execution scope inactive or changed");
        auto result = shared->request(estimate);
        if (result) current_context_execution_scope()->record_usage_decision(*result);
        if (result && json::get_string(*result, "status") == "requires_confirmation" &&
            json::get_string(estimate, "operation_id") == confirmation_operation) {
          if (const auto* confirmation = json::find(usage, "confirmation")) {
            result = shared->confirm(json::get_string(estimate, "operation_id"), json::get_string(*confirmation, "receipt_id"),
                (*confirmation)["approved"].get<bool>(), json::get_string(*confirmation, "ref"));
            if (result) current_context_execution_scope()->record_usage_decision(*result);
          }
        }
        if (result && json::get_string(*result, "status") == "allowed") {
#if defined(LOOM_VECTOR_USAGE_CLAIM)
          LOOM_TRY_ASSIGN(auto claim, claim_context_usage_operation(data_root, *result));
          current_context_execution_scope()->record_usage_decision(claim);
          if (!claim.value("claimed", false))
            return Error(Errc::Paused, "embedding operation already started; resolve unknown charge before retry");
#else
          (void)data_root;
          return Error(Errc::Unavailable, "embedding execution claim dependency unavailable");
#endif
        }
        return result;
      };
      policy.complete = [shared](std::string_view id, const Json& actual) {
        auto result = shared->complete(id, actual);
        if (result) {
          if (auto* scope = current_context_execution_scope()) scope->record_usage_decision(*result);
        }
        return result;
      };
      policy.cancel = [shared](std::string_view id, std::string_view reason) {
        auto result = shared->cancel(id, reason);
        if (result) {
          if (auto* scope = current_context_execution_scope()) scope->record_usage_decision(*result);
        }
        return result;
      };
      status["usage_policy"] = "available";
    } else status["usage_policy"] = "cached_only";
#else
    status["usage_policy"] = "requires_thread_2_integration";
#endif
    auto cache = std::make_shared<ProviderVectorCache>(rt.paths().root / "context_embedding_cache");
    engine.set_candidate_channel(channel_id, make_vector_candidate_channel(make_provider_vector_space(
        rt.providers(), rt.secrets(), rt.http(), std::move(request), std::move(cache), std::move(policy))));
    status["status"] = "available";
    status["source"] = "provider";
    return status;
  } catch (const std::exception&) { return Json{{"id", "vector"}, {"available", false}, {"status", "error"}, {"reason", "embedding_install_failed"}}; }
  catch (...) { return Json{{"id", "vector"}, {"available", false}, {"status", "error"}, {"reason", "embedding_install_failed"}}; }
}

}  // namespace loom::context
