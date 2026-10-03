// OWNER: wave 2 net/chat/worker. Port of engine/models.py (ModelRegistry) and
// the capability-based ProviderRegistry (MEGA MASTER 2.D, 4.5).
#include "loom/providers.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/util/fs.h"
#include "loom/util/utf8.h"
#include "stub.h"

namespace loom {

namespace {

std::string cfg_string(const Config& cfg, std::string_view key, std::string fallback = "") {
  Json v = cfg.get(key);
  return v.is_string() ? v.get<std::string>() : std::move(fallback);
}

std::string rstrip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

std::string last_segment(std::string_view id) {
  auto p = id.rfind('/');
  return p == std::string_view::npos ? std::string(id) : std::string(id.substr(p + 1));
}

Status save_models_file(const std::filesystem::path& path, const Json& models) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::string text = json::py_dumps(models, {.indent = 1, .ensure_ascii = false});
  return fsutil::write_file(path, text);
}

// v0.06.x / raw-API-cache legacy formats -> canonical list of {id,name,context_length,...}.
Json normalize_legacy_models(const Json& raw) {
  Json out = Json::array();
  if (raw.is_array()) {
    for (const auto& m : raw) {
      if (m.is_object() && m.contains("id")) {
        out.push_back(m);
      } else if (m.is_string()) {
        std::string id = m.get<std::string>();
        out.push_back(Json{{"id", id}, {"name", last_segment(id)}, {"context_length", 0}});
      }
      // Anything else is skipped silently, matching Python.
    }
  } else if (raw.is_object()) {
    const Json* data = json::find(raw, "data");
    if (!data || !data->is_array()) data = json::find(raw, "models");
    if (data && data->is_array()) {
      for (const auto& m : *data) {
        if (!m.is_object() || !m.contains("id")) continue;
        out.push_back(Json{{"id", m.at("id")},
                           {"name", m.contains("name") ? m.at("name") : m.at("id")},
                           {"context_length", m.contains("context_length") ? m.at("context_length") : Json(0)}});
      }
    }
  }
  return out;
}

// Seed manifests (policy data): OpenRouter, Anthropic (batch), Groq, Google
// Speech, OCR.space, GitHub — matches engine/*.py's hardcoded providers.
constexpr std::string_view kBuiltinManifestsJson = R"JSON(
[
  {
    "id": "openrouter",
    "display_name": "OpenRouter",
    "auth_secret": "api_key",
    "auth_scheme": "bearer",
    "capabilities": [
      {"resource": "llm", "name": "chat.completions"},
      {"resource": "llm", "name": "chat.stream"},
      {"resource": "llm", "name": "models.list"},
      {"resource": "llm", "name": "web_search"},
      {"resource": "llm", "name": "reasoning"}
    ]
  },
  {
    "id": "anthropic",
    "display_name": "Anthropic",
    "base_url": "https://api.anthropic.com",
    "auth_secret": "anthropic_batch_key",
    "auth_scheme": "x-api-key",
    "capabilities": [
      {"resource": "batch", "name": "messages.batches"}
    ]
  },
  {
    "id": "groq",
    "display_name": "Groq",
    "auth_secret": "groq_api_key",
    "auth_scheme": "bearer",
    "capabilities": [
      {"resource": "asr", "name": "transcribe",
       "constraints": {"formats": ["wav", "mp3", "m4a", "flac", "ogg"], "max_file_mb": 25}}
    ]
  },
  {
    "id": "google_speech",
    "display_name": "Google Speech",
    "auth_secret": "google_speech_api_key",
    "auth_scheme": "query:key",
    "capabilities": [
      {"resource": "asr", "name": "transcribe"}
    ]
  },
  {
    "id": "ocr_space",
    "display_name": "OCR.space",
    "auth_secret": "ocr_space_api_key",
    "auth_scheme": "form:apikey",
    "capabilities": [
      {"resource": "ocr", "name": "recognize"}
    ]
  },
  {
    "id": "github",
    "display_name": "GitHub",
    "auth_secret": "github_token",
    "auth_scheme": "token",
    "capabilities": [
      {"resource": "vcs", "name": "repo.sync"}
    ]
  }
]
)JSON";

}  // namespace

// ── ModelRegistry ─────────────────────────────────────────────────────

ModelRegistry::ModelRegistry(std::filesystem::path path, const Config& cfg, const Secrets& secrets,
                             net::HttpTransport& http)
    : path_(std::move(path)), cfg_(cfg), secrets_(secrets), http_(http) {
  (void)reload();
}

Status ModelRegistry::reload() {
  std::lock_guard lk(mu_);
  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) {
    models_ = Json::array();
    return ok_status();
  }

  auto text = fsutil::read_file(path_);
  bool corrupted = !text;
  Json models = Json::array();
  if (text) {
    if (utf8::is_blank(*text)) {
      models_ = Json::array();
      return ok_status();
    }
    auto parsed = json::parse(*text);
    if (!parsed) {
      corrupted = true;
    } else {
      models = normalize_legacy_models(*parsed);
    }
  }

  if (corrupted) {
    log::warn("loom.models", "Corrupted model cache — removing {}", path_.string());
    std::filesystem::remove(path_, ec);
    models_ = Json::array();
    return ok_status();
  }

  models_ = models;
  if (!models_.empty()) {
    log::info("loom.models", "Loaded {} cached models", models_.size());
    if (auto st = save_models_file(path_, models_); !st) {
      log::warn("loom.models", "failed to re-save model cache: {}", st.error().message);
    }
  } else {
    log::info("loom.models", "Model cache empty or unrecognized — will refresh from API");
  }
  return ok_status();
}

Status ModelRegistry::update_from_api() {
  std::string key = secrets_.get_string("api_key");
  std::string base = rstrip_slash(cfg_string(cfg_, "base_url"));
  if (key.empty() || base.empty()) {
    log::warn("loom.models", "Cannot refresh models — no API key or base URL");
    return Error(Errc::Unavailable, "no API key or base URL configured");
  }

  net::HttpRequest req;
  req.method = "GET";
  req.url = base + "/models";
  req.headers = {{"Authorization", "Bearer " + key}};
  req.timeout_ms = 15000;
  auto resp = http_.send(req);
  if (!resp) return resp.error();
  if (!resp->ok()) {
    return Error(Errc::Http, "API error " + std::to_string(resp->status) + ": " + std::string(utf8::prefix(resp->body, 500)));
  }
  auto j = resp->json();
  if (!j) return Error(Errc::Parse, "invalid JSON from /models");

  Json models = Json::array();
  if (const Json* data = json::find(*j, "data"); data && data->is_array()) {
    for (const auto& m : *data) {
      if (!m.is_object() || !m.contains("id")) continue;
      models.push_back(Json{{"id", m.at("id")},
                            {"name", m.contains("name") ? m.at("name") : m.at("id")},
                            {"context_length", m.contains("context_length") ? m.at("context_length") : Json(0)},
                            {"pricing", m.contains("pricing") ? m.at("pricing") : Json::object()},
                            {"description", m.contains("description") ? m.at("description") : Json("")}});
    }
  }

  {
    std::lock_guard lk(mu_);
    models_ = models;
  }
  if (auto st = save_models_file(path_, models); !st) {
    log::warn("loom.models", "failed to save models.json: {}", st.error().message);
  }
  log::info("loom.models", "Refreshed model list: {} models", models.size());
  return ok_status();
}

Json ModelRegistry::all() const {
  std::lock_guard lk(mu_);
  return models_;
}

std::string ModelRegistry::name(std::string_view model_id) const {
  std::lock_guard lk(mu_);
  for (const auto& m : models_) {
    if (json::get_string(m, "id") != model_id) continue;
    if (const Json* nm = json::find(m, "name"); nm && nm->is_string()) return nm->get<std::string>();
    return last_segment(model_id);
  }
  return last_segment(model_id);
}

Json ModelRegistry::grouped() const {
  std::lock_guard lk(mu_);
  Json out = Json::object();
  for (const auto& m : models_) {
    std::string id = json::get_string(m, "id");
    auto p = id.find('/');
    std::string provider = p == std::string::npos ? "other" : id.substr(0, p);
    if (!out.contains(provider)) out[provider] = Json::array();
    out[provider].push_back(m);
  }
  return out;
}

Json ModelRegistry::get_pricing(std::string_view model_id) const {
  std::lock_guard lk(mu_);
  for (const auto& m : models_) {
    if (json::get_string(m, "id") != model_id) continue;
    if (const Json* p = json::find(m, "pricing"); p && p->is_object()) return *p;
    return Json::object();
  }
  return Json::object();
}

std::string ModelRegistry::get_description(std::string_view model_id) const {
  std::lock_guard lk(mu_);
  for (const auto& m : models_) {
    if (json::get_string(m, "id") == model_id) return json::get_string(m, "description");
  }
  return {};
}

std::int64_t ModelRegistry::get_context_length(std::string_view model_id) const {
  std::lock_guard lk(mu_);
  for (const auto& m : models_) {
    if (json::get_string(m, "id") == model_id) return json::get_int(m, "context_length", 0);
  }
  return 0;
}

double ModelRegistry::estimate_cost(std::string_view model_id, std::int64_t input_tokens,
                                    std::int64_t output_tokens) const {
  Json pricing = get_pricing(model_id);
  if (pricing.empty()) return 0.0;
  auto price_of = [&](const char* key) -> double {
    const Json* v = json::find(pricing, key);
    if (!v) return 0.0;
    if (v->is_number()) return v->get<double>();
    if (v->is_string()) {
      try {
        return std::stod(v->get<std::string>());
      } catch (const std::exception&) {
        return 0.0;
      }
    }
    return 0.0;
  };
  double input_price = price_of("prompt");
  double output_price = price_of("completion");
  double total = (static_cast<double>(input_tokens) * input_price + static_cast<double>(output_tokens) * output_price) /
                 1'000'000.0;
  double scaled = total * 1e8;
  return std::round(scaled) / 1e8;
}

std::size_t ModelRegistry::size() const {
  std::lock_guard lk(mu_);
  return models_.size();
}

// ── Capability / ProviderManifest ───────────────────────────────────

Json Capability::to_json() const { return Json{{"resource", resource}, {"name", name}, {"constraints", constraints}}; }

Json ProviderManifest::to_json() const {
  Json caps = Json::array();
  for (const auto& c : capabilities) caps.push_back(c.to_json());
  return Json{{"id", id},
              {"display_name", display_name},
              {"base_url", base_url},
              {"auth_secret", auth_secret},
              {"auth_scheme", auth_scheme},
              {"default_headers", default_headers},
              {"capabilities", caps},
              {"metadata", metadata}};
}

Result<ProviderManifest> ProviderManifest::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "provider manifest must be an object");
  ProviderManifest m;
  m.id = json::get_string(j, "id");
  if (m.id.empty()) return Error(Errc::InvalidArgument, "provider manifest needs an id");
  m.display_name = json::get_string(j, "display_name", m.id);
  m.base_url = json::get_string(j, "base_url");
  m.auth_secret = json::get_string(j, "auth_secret");
  m.auth_scheme = json::get_string(j, "auth_scheme", "none");
  if (const Json* h = json::find(j, "default_headers"); h && h->is_object()) m.default_headers = *h;
  if (const Json* md = json::find(j, "metadata"); md && md->is_object()) m.metadata = *md;
  if (const Json* caps = json::find(j, "capabilities"); caps && caps->is_array()) {
    for (const auto& c : *caps) {
      if (!c.is_object()) continue;
      Capability cap;
      cap.resource = json::get_string(c, "resource");
      cap.name = json::get_string(c, "name");
      if (cap.resource.empty() || cap.name.empty()) {
        return Error(Errc::InvalidArgument, "capability needs a resource and a name");
      }
      if (const Json* cons = json::find(c, "constraints"); cons && cons->is_object()) cap.constraints = *cons;
      m.capabilities.push_back(std::move(cap));
    }
  }
  return m;
}

bool constraints_satisfied(const Json& offered, const Json& requested) {
  if (!requested.is_object() || requested.empty()) return true;
  if (!offered.is_object()) return false;
  for (auto it = requested.begin(); it != requested.end(); ++it) {
    const std::string& key = it.key();
    const Json& want = it.value();
    const Json* have = json::find(offered, key);
    if (!have) return false;

    if (have->is_array()) {
      auto has_element = [&](const Json& needle) {
        for (const auto& h : *have) {
          if (h == needle) return true;
        }
        return false;
      };
      if (want.is_array()) {
        for (const auto& w : want) {
          if (!has_element(w)) return false;
        }
      } else if (!has_element(want)) {
        return false;
      }
    } else if (key.rfind("max_", 0) == 0 && have->is_number() && want.is_number()) {
      if (!(want.get<double>() <= have->get<double>())) return false;
    } else if (key.rfind("min_", 0) == 0 && have->is_number() && want.is_number()) {
      if (!(want.get<double>() >= have->get<double>())) return false;
    } else if (have->is_boolean()) {
      if (want.is_boolean() && want.get<bool>() && !have->get<bool>()) return false;
    } else if (*have != want) {
      return false;
    }
  }
  return true;
}

// ── ProviderRegistry ──────────────────────────────────────────────────

ProviderRegistry::ProviderRegistry(const Config& cfg, const Secrets& secrets) : cfg_(cfg), secrets_(secrets) {}

Status ProviderRegistry::load_builtin() {
  auto parsed = json::parse(kBuiltinManifestsJson);
  if (!parsed) return Error(Errc::Internal, "builtin provider manifests are invalid JSON");
  if (auto st = load(*parsed); !st) return st;

  std::string base = cfg_string(cfg_, "base_url");
  if (!base.empty()) {
    std::lock_guard lk(mu_);
    for (auto& m : manifests_) {
      if (m.id == "openrouter") m.base_url = base;
    }
  }
  return ok_status();
}

Status ProviderRegistry::load(const Json& manifests) {
  if (!manifests.is_array()) return Error(Errc::InvalidArgument, "provider manifests must be a JSON array");
  std::vector<ProviderManifest> parsed;
  parsed.reserve(manifests.size());
  for (const auto& m : manifests) {
    auto r = ProviderManifest::from_json(m);
    if (!r) return r.error();
    parsed.push_back(std::move(r).value());
  }
  std::lock_guard lk(mu_);
  for (auto& pm : parsed) {
    auto it = std::find_if(manifests_.begin(), manifests_.end(), [&](const ProviderManifest& e) { return e.id == pm.id; });
    if (it != manifests_.end()) {
      *it = std::move(pm);
    } else {
      manifests_.push_back(std::move(pm));
    }
  }
  return ok_status();
}

bool ProviderRegistry::available(const ProviderManifest& m) const {
  if (m.auth_secret.empty()) return true;
  return secrets_.has(m.auth_secret);
}

std::vector<ProviderManifest> ProviderRegistry::find(std::string_view resource, std::string_view capability,
                                                      const Json& constraints) const {
  std::lock_guard lk(mu_);
  std::vector<ProviderManifest> out;
  for (const auto& m : manifests_) {
    if (!available(m)) continue;
    for (const auto& c : m.capabilities) {
      if (c.resource == resource && c.name == capability && constraints_satisfied(c.constraints, constraints)) {
        out.push_back(m);
        break;
      }
    }
  }
  return out;
}

bool ProviderRegistry::can(std::string_view resource, std::string_view capability, const Json& constraints) const {
  return !find(resource, capability, constraints).empty();
}

std::optional<ProviderManifest> ProviderRegistry::get(std::string_view id) const {
  std::lock_guard lk(mu_);
  for (const auto& m : manifests_) {
    if (m.id == id) return m;
  }
  return std::nullopt;
}

std::vector<ProviderManifest> ProviderRegistry::all() const {
  std::lock_guard lk(mu_);
  return manifests_;
}

Json ProviderRegistry::status() const {
  std::lock_guard lk(mu_);
  Json arr = Json::array();
  for (const auto& m : manifests_) {
    Json caps = Json::array();
    for (const auto& c : m.capabilities) caps.push_back(c.name);
    arr.push_back(Json{{"id", m.id}, {"available", available(m)}, {"capabilities", caps}});
  }
  return Json{{"providers", arr}};
}

}  // namespace loom
