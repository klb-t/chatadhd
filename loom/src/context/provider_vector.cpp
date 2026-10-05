#include "provider_vector.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>

#include "loom/context_engine.h"
#include "loom/runtime.h"
#include "loom/selector.h"
#include "loom/sqlite.h"
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
  using Embed = std::function<Result<providers::EmbeddingReply>(
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
        LOOM_TRY_ASSIGN(auto batch, embed_(identity, missing));
        auto& values = batch.vectors;
        LOOM_TRY(valid_vectors(values, missing.size()));
        // Corpus and query arrive in separate calls. The representation binding
        // must survive both fresh spaces and a durable-cache reopen.
        LOOM_TRY(cache_->observe(identity, values.front().size(), batch.reported_model));
        // Check against existing cache dimensions before writing any fresh
        // value: a changed provider representation must not be silently mixed.
        std::optional<std::size_t> dimensions;
        for (const auto& vector : dense) if (!vector.empty()) {
          if (dimensions && *dimensions != vector.size()) return Error(Errc::Parse, "cached embedding dimensions inconsistent");
          dimensions = vector.size();
        }
        if (dimensions && *dimensions != values.front().size())
          return Error(Errc::Parse, "provider embedding dimensions changed");
        for (std::size_t i = 0; i < hashes.size(); ++i)
          LOOM_TRY(cache_->put(identity, hashes[i], values[i], batch.reported_model, batch.response_source));
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
  if (existing != vectors_.end()) {
    LOOM_TRY(observe_locked(identity, existing->second.size(), reported_models_[key]));
    return std::optional<std::vector<float>>(existing->second);
  }
  if (directory_.empty()) return std::optional<std::vector<float>>{};
  const auto path = directory_ / (key + ".json");
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec) return Error(Errc::Io, "embedding cache lookup failed");
  if (!exists) return std::optional<std::vector<float>>{};
  LOOM_TRY_ASSIGN(auto text, fsutil::read_file(path));
  auto parsed = json::parse(text);
  if (!parsed) return Error(Errc::Parse, "embedding cache invalid");
  const Json* values = &*parsed;
  std::optional<std::string> reported_model;
  if (parsed->is_object()) {
    if (json::get_string(*parsed, "schema") != "loom.embedding_cache/2")
      return Error(Errc::Parse, "embedding cache schema invalid");
    values = json::find(*parsed, "vector");
    if (const auto* model = json::find(*parsed, "reported_model"); model && !model->is_null()) {
      if (!model->is_string() || model->get_ref<const std::string&>().empty())
        return Error(Errc::Parse, "embedding cache model invalid");
      reported_model = model->get<std::string>();
    }
  }
  if (!values || !values->is_array()) return Error(Errc::Parse, "embedding cache invalid");
  std::vector<float> vector;
  for (const auto& value : *values) {
    if (!value.is_number() || !std::isfinite(value.get<double>()) ||
        std::abs(value.get<double>()) > std::numeric_limits<float>::max() ||
        (value.get<double>() != 0 && value.get<float>() == 0))
      return Error(Errc::Parse, "embedding cache vector invalid");
    vector.push_back(value.get<float>());
  }
  LOOM_TRY(valid_vectors({vector}, 1));
  LOOM_TRY(observe_locked(identity, vector.size(), reported_model));
  vectors_[key] = vector;
  reported_models_[key] = reported_model;
  return std::optional<std::vector<float>>(std::move(vector));
}

Status ProviderVectorCache::put(std::string_view identity, std::string_view hash, const std::vector<float>& vector,
                               const std::optional<std::string>& reported_model, const Json& response_source) {
  LOOM_TRY(valid_vectors({vector}, 1));
  std::lock_guard lock(mutex_);
  LOOM_TRY(observe_locked(identity, vector.size(), reported_model));
  const auto key = cache_key(identity, hash);
  if (!directory_.empty()) {
    // Different engines/processes share this derived cache. Unique temporary
    // files avoid atomic_write's fixed .tmp name racing for the same key.
    const auto temporary = directory_ / (key + "." + random_hex(32) + ".json");
    Json entry{{"schema", "loom.embedding_cache/2"}, {"vector", vector},
        {"reported_model", reported_model ? Json(*reported_model) : Json(nullptr)},
        {"response_source", response_source}};
    auto written = fsutil::atomic_write(temporary, json::dump(entry), {.owner_only = true});
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
  reported_models_[key] = reported_model;
  return {};
}

Status ProviderVectorCache::observe(std::string_view identity, std::size_t dimensions,
                                   const std::optional<std::string>& reported_model) {
  std::lock_guard lock(mutex_);
  return observe_locked(identity, dimensions, reported_model);
}

Status ProviderVectorCache::observe_locked(std::string_view identity, std::size_t dimensions,
                                          const std::optional<std::string>& reported_model) {
  if (dimensions == 0 || (reported_model && reported_model->empty()))
    return Error(Errc::Parse, "embedding representation invalid");
  const auto key = std::string(identity);
  const auto dimension_text = std::to_string(dimensions);
  auto existing = representations_.find(key);
  if (existing != representations_.end()) {
    if (existing->second["dimensions"] != dimensions)
      return Error(Errc::Conflict, "embedding representation dimensions changed");
    const auto* model = json::find(existing->second, "reported_model");
    if (reported_model && model && model->is_string() && *model != *reported_model)
      return Error(Errc::Conflict, "embedding representation reported model changed");
    // A known binding is immutable. A first known report still needs the
    // shared transaction so another process's first report cannot be erased.
    if (!reported_model || (model && model->is_string())) return {};
  }
  std::optional<std::string> effective_model = reported_model;
  if (!directory_.empty()) {
    LOOM_TRY(fsutil::ensure_dir(directory_));
    LOOM_TRY_ASSIGN(auto database, sql::Connection::open(directory_ / "representations.sqlite"));
    LOOM_TRY(fsutil::chmod_owner_only(directory_ / "representations.sqlite"));
    LOOM_TRY(database.exec("CREATE TABLE IF NOT EXISTS embedding_representations("
        "identity TEXT PRIMARY KEY, dimensions TEXT NOT NULL, reported_model TEXT)"));
    sql::Txn transaction(database);
    LOOM_TRY(transaction.begin_status());
    LOOM_TRY(database.run("INSERT OR IGNORE INTO embedding_representations VALUES(?,?,?)",
                         identity, dimension_text, reported_model));
    LOOM_TRY_ASSIGN(auto row, database.prepare(
        "SELECT dimensions,reported_model FROM embedding_representations WHERE identity=?"));
    row.bind(1, identity);
    LOOM_TRY_ASSIGN(auto found, row.step());
    if (!found) return Error(Errc::Database, "embedding representation binding unavailable");
    if (row.get_text(0) != dimension_text)
      return Error(Errc::Conflict, "embedding representation dimensions changed");
    effective_model = row.get_opt_text(1);
    if (reported_model && effective_model && *reported_model != *effective_model)
      return Error(Errc::Conflict, "embedding representation reported model changed");
    if (reported_model && !effective_model) {
      LOOM_TRY(database.run("UPDATE embedding_representations SET reported_model=? WHERE identity=? AND reported_model IS NULL",
                           *reported_model, identity));
      effective_model = reported_model;
    }
    LOOM_TRY(transaction.commit());
  }
  representations_[key] = Json{{"dimensions", dimensions},
      {"reported_model", effective_model ? Json(*effective_model) : Json(nullptr)}};
  return {};
}

Result<Json> ProviderVectorCache::inspection(std::string_view identity) {
  std::lock_guard lock(mutex_);
  auto found = representations_.find(std::string(identity));
  if (found != representations_.end()) return found->second;
  if (directory_.empty()) return Json(nullptr);
  std::error_code error;
  const auto path = directory_ / "representations.sqlite";
  const bool exists = std::filesystem::exists(path, error);
  if (error) return Error(Errc::Io, "embedding representation lookup failed");
  if (!exists) return Json(nullptr);
  LOOM_TRY_ASSIGN(auto database, sql::Connection::open(path, {.create = false, .read_only = true}));
  LOOM_TRY_ASSIGN(auto row, database.prepare(
      "SELECT dimensions,reported_model FROM embedding_representations WHERE identity=?"));
  row.bind(1, identity);
  LOOM_TRY_ASSIGN(auto exists_row, row.step());
  if (!exists_row) return Json(nullptr);
  auto dimension = json::parse(row.get_text(0));
  if (!dimension || !dimension->is_number_unsigned() || dimension->get<std::uint64_t>() == 0 ||
      dimension->get<std::uint64_t>() > std::numeric_limits<std::size_t>::max())
    return Error(Errc::Parse, "embedding representation dimension invalid");
  const auto model = row.get_opt_text(1);
  return Json{{"dimensions", *dimension}, {"reported_model", model ? Json(*model) : Json(nullptr)}};
}

std::size_t ProviderVectorCache::size() const { std::lock_guard lock(mutex_); return vectors_.size(); }

std::shared_ptr<resolve::VectorSpace> make_provider_vector_space(
    const ProviderRegistry& registry, const Secrets& secrets, net::HttpTransport& http,
    providers::EmbeddingRequest request, std::shared_ptr<ProviderVectorCache> cache, ProviderVectorPolicy policy) {
  const auto method = "embedding:" + request.provider_id + ":" + request.model;
  auto identity = [&registry, &secrets, request]() -> Result<std::string> {
    LOOM_TRY_ASSIGN(auto current, providers::embedding_identity(registry, secrets, request));
    if (!request.expected_identity.empty() && request.expected_identity != current)
      return Error(Errc::Conflict, "embedding installed identity changed before lookup");
    return current;
  };
  auto embed = [&registry, &secrets, &http, request, policy, pending = std::map<std::string, std::string>{}](const std::string& expected,
      const std::vector<std::string>& texts) mutable -> Result<providers::EmbeddingReply> {
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
        {"embedding_identity", expected}, {"accepted_reported_models", request.accepted_reported_models},
        {"request_options_sha256", Sha256::hex(json::canonical(request.request_options))},
        {"timeout_ms", request.timeout_ms},
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
      actual["requested_model"] = reply->requested_model;
      actual["reported_model"] = reply->reported_model ? Json(*reply->reported_model) : Json(nullptr);
      actual["model_attestation"] = reply->reported_model ? "provider_reported" : "unknown";
    }
    // Even a timeout/malformed response may incur a charge. Unknown quantities
    // retain their ledger reservation; cancellation never invents free usage.
    auto accounted = policy.complete(operation, actual);
    if (!accounted) return Error(accounted.error().code, "embedding accounting failed after attempted request");
    if (!reply) return reply.error();
    return std::move(*reply);
  };
  return std::make_shared<CachedVectorSpace>(method, std::move(identity), std::move(embed), std::move(cache));
}

std::shared_ptr<resolve::VectorSpace> make_injected_vector_space(
    std::shared_ptr<EmbeddingProvider> provider, std::shared_ptr<ProviderVectorCache> cache,
    std::function<bool()> callable_on_miss, std::optional<std::string> expected_model) {
  if (!provider) return nullptr;
  auto shared = injected_identity(provider, std::move(cache));
  auto identity = [provider, nonce = shared.nonce, expected_model]() -> Result<std::string> {
    const auto model = provider->model_id();
    if (expected_model && *expected_model != model)
      return Error(Errc::Conflict, "injected embedding installed model changed before lookup");
    return Sha256::hex(json::canonical(Json::array({"injected", nonce, model})));
  };
  auto embed = [provider, callable_on_miss, nonce = shared.nonce](const std::string& expected, const std::vector<std::string>& texts)
      -> Result<providers::EmbeddingReply> {
    if (callable_on_miss && !callable_on_miss())
      return Error(Errc::Unavailable, "injected embedding execution scope inactive");
    const auto model = provider->model_id();
    if (Sha256::hex(json::canonical(Json::array({"injected", nonce, model}))) != expected)
      return Error(Errc::Conflict, "injected embedding identity changed before request");
    LOOM_TRY_ASSIGN(auto vectors, provider->embed(texts));
    if (provider->model_id() != model)
      return Error(Errc::Conflict, "injected embedding model changed during request");
    providers::EmbeddingReply reply;
    reply.vectors = std::move(vectors);
    reply.requested_model = model;
    return reply;  // Native model_id is a declaration, never live attestation.
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
    Json status{{"id", channel_id}, {"available", false}, {"status", "unavailable"}, {"offline_cache", true},
        {"readiness", "unavailable"}, {"live_verification", "not_performed"},
        {"model_attestation", "unknown"}, {"cache_coverage", "unknown_until_inputs"}, {"miss_authorized", false},
        {"installed_this_call", false}, {"binding_status", "unavailable"}};
    status["authorization"] = Json{{"scope_active", false}, {"scope_options_match", false},
        {"host_allows_provider_calls", allow_provider_calls},
        {"configured_enabled", bool_setting(section, "enabled", false)},
        {"configured_calls_authorized", bool_setting(section, "calls_authorized", false)}};
    if (channel_id.empty()) { status["reason"] = "embedding_channel_id_empty"; return status; }
    if (settings && !settings->is_object()) { status["reason"] = "embedding_options_invalid"; return status; }
    for (const auto* key : {"enabled", "calls_authorized"}) {
      const auto* value = json::find(section, key);
      if (value && !value->is_boolean()) { status["reason"] = "embedding_boolean_option_invalid"; return status; }
    }
    if (settings && !bool_setting(section, "enabled", false)) { status["reason"] = "embedding_disabled"; return status; }
#if defined(LOOM_VECTOR_EXECUTION_SCOPE)
    const auto* active_scope = current_context_execution_scope();
    const auto* active_embedding = active_scope ? json::find(active_scope->options(), "embedding") : nullptr;
    status["authorization"]["scope_active"] = active_scope != nullptr;
    status["authorization"]["scope_options_match"] = active_embedding && *active_embedding == section;
    status["miss_authorized"] = allow_provider_calls && active_embedding && *active_embedding == section &&
        bool_setting(section, "enabled", false) && bool_setting(section, "calls_authorized", false);
#endif
    if (auto injected = rt.embedding_provider()) {
      // Constructor supplies the native capability once. Absent scoped options
      // do not replace an explicit externally installed candidate channel later.
      const auto model = injected->model_id();
      status["model"] = model;
      status["requested_model"] = model;
      status["reported_model"] = nullptr;
      auto shared = injected_identity(injected, nullptr);
      const auto identity = Sha256::hex(json::canonical(Json::array({"injected", shared.nonce, model})));
      status["identity_sha256"] = identity;
      status["actual_consumed_parameters"] = Json{{"channel_id", channel_id}, {"model", model},
          {"enabled", bool_setting(section, "enabled", false)},
          {"calls_authorized", bool_setting(section, "calls_authorized", false)},
          {"allow_provider_calls", allow_provider_calls}, {"provider_source", "native_injection"}};
      if (!allow_provider_calls || settings) {
        auto callable = [allow_provider_calls, section, injected, model] {
          if (injected->model_id() != model) return false;
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
            make_injected_vector_space(std::move(injected), nullptr, std::move(callable), model)));
        status["installed_this_call"] = true;
        status["binding_status"] = "installed_snapshot";
      } else {
        // Preserve explicit external injections without pretending that their
        // execution parameters are the runtime provider's descriptor.
        status.erase("actual_consumed_parameters");
        status.erase("identity_sha256");
        status["binding_status"] = "existing_channel_not_inspected";
      }
      status["available"] = true;
      status["status"] = "available";
      status["source"] = "native_injection";
      status["readiness"] = "native_injection_declared";
      status["calls_authorized"] = allow_provider_calls && settings && bool_setting(section, "calls_authorized", false);
      return status;
    }
    if (!settings) { status["reason"] = "embedding_not_configured"; return status; }
    providers::EmbeddingRequest request;
    request.provider_id = json::get_string(section, "provider_id");
    request.model = json::get_string(section, "model");
    if (const auto* models = json::find(section, "accepted_reported_models")) {
      if (!models->is_array()) { status["reason"] = "embedding_model_binding_invalid"; return status; }
      for (const auto& model : *models) {
        if (!model.is_string() || model.get_ref<const std::string&>().empty()) {
          status["reason"] = "embedding_model_binding_invalid"; return status;
        }
        request.accepted_reported_models.push_back(model.get<std::string>());
      }
    }
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
    auto identity_result = providers::embedding_identity(rt.providers(), rt.secrets(), request);
    if (!identity_result) { status["available"] = false; status["reason"] = "embedding_identity_unavailable"; return status; }
    const auto identity = *identity_result;
    request.expected_identity = identity;
    status["identity_sha256"] = identity;
    status["actual_consumed_parameters"] = Json{{"channel_id", channel_id},
        {"provider_id", request.provider_id}, {"model", request.model}, {"timeout_ms", request.timeout_ms},
        {"accepted_reported_models", request.accepted_reported_models}, {"request_options", request.request_options},
        {"encoding_format", "float"}, {"calls_authorized", request.calls_authorized}, {"provider_source", "provider"}};
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
    status["miss_authorized"] = false;
#endif
    auto cache = std::make_shared<ProviderVectorCache>(rt.paths().root / "context_embedding_cache");
    {
      auto observed = cache->inspection(identity);
      if (!observed) { status["available"] = false; status["reason"] = "embedding_cache_binding_invalid"; return status; }
      status["cached_representation"] = *observed;
      // The sidecar reports an observed space, not proof that every cached
      // entry contained a model report or that a future response is verified.
      status["cache_binding"] = observed->is_null() ? "unobserved" : "historically_observed";
    }
    engine.set_candidate_channel(channel_id, make_vector_candidate_channel(make_provider_vector_space(
        rt.providers(), rt.secrets(), rt.http(), std::move(request), std::move(cache), std::move(policy))));
    status["installed_this_call"] = true;
    status["binding_status"] = "installed_snapshot";
    status["status"] = "available";
    status["source"] = "provider";
    return status;
  } catch (const std::exception&) { return Json{{"id", "vector"}, {"available", false}, {"status", "error"}, {"reason", "embedding_install_failed"}}; }
  catch (...) { return Json{{"id", "vector"}, {"available", false}, {"status", "error"}, {"reason", "embedding_install_failed"}}; }
}

}  // namespace loom::context
