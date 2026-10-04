// loom/providers.h — model registry (port of engine/models.py) and the
// capability-based provider registry (MEGA MASTER 2.D, 4.5). [OWNER: wave 2 net/chat/worker]
#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Config;
class Secrets;
namespace net {
class HttpTransport;
}

// ── ModelRegistry (engine/models.py) ────────────────────────────────
// models.json formats accepted on load (then re-saved canonically, indent=1,
// ensure_ascii=False, non-atomic like Python):
//   * list of model dicts with "id"          (canonical)
//   * list of plain id strings               (v0.06.x) -> {"id","name":id.split("/")[-1],"context_length":0}
//   * dict {"data"|"models": [...]}          (raw API cache) -> {"id","name","context_length"}
// Empty/unrecognised -> empty list; corrupt JSON -> file deleted, empty list.
// update_from_api(): GET {base_url}/models with Bearer api_key (timeout 15 s);
//   keeps {"id","name"(default id),"context_length"(0),"pricing"({}),
//   "description"("")} per model and saves. Missing key/base -> Unavailable.
class ModelRegistry {
 public:
  ModelRegistry(std::filesystem::path path, const Config& cfg, const Secrets& secrets, net::HttpTransport& http);

  Status update_from_api();
  Json all() const;                                   // JSON array (copy)
  std::string name(std::string_view model_id) const;  // registry name, else id after last '/'
  // {provider: [models]} with provider = id before the first '/' or "other";
  // providers in first-appearance order.
  Json grouped() const;
  Json get_pricing(std::string_view model_id) const;  // {} when unknown
  std::string get_description(std::string_view model_id) const;
  std::int64_t get_context_length(std::string_view model_id) const;
  // (in * prompt + out * completion) / 1e6 rounded to 8 decimals. Prices may
  // be numbers or numeric strings (OpenRouter sends strings; Python would have
  // mis-multiplied them - Loom parses them).
  double estimate_cost(std::string_view model_id, std::int64_t input_tokens, std::int64_t output_tokens) const;

  Status reload();
  std::size_t size() const;

 private:
  std::filesystem::path path_;
  const Config& cfg_;
  const Secrets& secrets_;
  net::HttpTransport& http_;
  mutable std::mutex mu_;
  Json models_ = Json::array();
};

// ── Capability registry ─────────────────────────────────────────────
// Providers are data (manifests); code asks can(resource, capability,
// constraints) instead of "if provider == X".
struct Capability {
  std::string resource;  // "llm" | "embedding" | "asr" | "ocr" | "batch" | "vcs" | ...
  std::string name;      // "chat.completions" | "chat.stream" | "models.list" | "web_search" |
                         // "reasoning" | "transcribe" | "recognize" | "messages.batches" | "repo.sync" ...
  Json constraints = Json::object();  // offered limits, e.g. {"formats":["wav","mp3"],"max_file_mb":25}
  Json to_json() const;
};

struct ProviderManifest {
  std::string id;            // "openrouter", "anthropic", "groq", "google_speech", "ocr_space", "github"
  std::string display_name;
  std::string base_url;
  std::string auth_secret;   // secrets.json key holding the credential ("" = none needed)
  std::string auth_scheme;   // "bearer" | "x-api-key" | "token" | "query:key" | "form:apikey" | "none"
  Json default_headers = Json::object();
  std::vector<Capability> capabilities;
  Json metadata = Json::object();
  Json to_json() const;
  static Result<ProviderManifest> from_json(const Json& j);
};

// Constraint matching (requested vs offered, per key; all keys must match):
//   offered array  : requested scalar must be an element; requested array a subset
//   key "max_*"    : requested number <= offered number
//   key "min_*"    : requested number >= offered number
//   offered bool   : requested true requires offered true
//   otherwise      : equality
//   key missing in offered constraints -> not satisfied
bool constraints_satisfied(const Json& offered, const Json& requested);

class ProviderRegistry {
 public:
  ProviderRegistry(const Config& cfg, const Secrets& secrets);

  // Built-in manifests (policy data): openrouter (llm chat.completions,
  // chat.stream, models.list, web_search, reasoning; auth api_key/bearer;
  // base_url from config), anthropic (batch messages.batches; auth
  // anthropic_batch_key/x-api-key), groq (asr transcribe; groq_api_key),
  // google_speech (asr transcribe; google_speech_api_key/query:key),
  // ocr_space (ocr recognize; ocr_space_api_key/form:apikey), github (vcs
  // repo.sync; github_token/token).
  Status load_builtin();
  Status load(const Json& manifests);  // upsert by id

  // A provider is available when its auth secret is present (or none needed).
  bool available(const ProviderManifest& m) const;
  bool can(std::string_view resource, std::string_view capability, const Json& constraints = Json::object()) const;
  // Matching available providers in registration order (routing policy v1).
  std::vector<ProviderManifest> find(std::string_view resource, std::string_view capability,
                                     const Json& constraints = Json::object()) const;
  std::optional<ProviderManifest> get(std::string_view id) const;
  std::vector<ProviderManifest> all() const;
  // {"providers":[{"id","available","capabilities":[...]}]} — never includes secrets.
  Json status() const;

 private:
  const Config& cfg_;
  const Secrets& secrets_;
  mutable std::mutex mu_;
  std::vector<ProviderManifest> manifests_;
};

}  // namespace loom
