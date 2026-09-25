// OWNER: wave 2 net/chat/worker. Stub.
#include "loom/providers.h"

#include "stub.h"

namespace loom {

ModelRegistry::ModelRegistry(std::filesystem::path path, const Config& cfg, const Secrets& secrets,
                             net::HttpTransport& http)
    : path_(std::move(path)), cfg_(cfg), secrets_(secrets), http_(http) {
  // STUB: wave2 - load models.json (all legacy formats)
}

Status ModelRegistry::update_from_api() { return LOOM_NOT_IMPLEMENTED("ModelRegistry::update_from_api"); }  // STUB: wave2
Json ModelRegistry::all() const {
  std::lock_guard lk(mu_);
  return models_;
}
std::string ModelRegistry::name(std::string_view model_id) const {
  return std::string(model_id);  // STUB: wave2
}
Json ModelRegistry::grouped() const { return Json::object(); }                        // STUB: wave2
Json ModelRegistry::get_pricing(std::string_view) const { return Json::object(); }    // STUB: wave2
std::string ModelRegistry::get_description(std::string_view) const { return {}; }    // STUB: wave2
std::int64_t ModelRegistry::get_context_length(std::string_view) const { return 0; }  // STUB: wave2
double ModelRegistry::estimate_cost(std::string_view, std::int64_t, std::int64_t) const { return 0.0; }  // STUB: wave2
Status ModelRegistry::reload() { return LOOM_NOT_IMPLEMENTED("ModelRegistry::reload"); }  // STUB: wave2
std::size_t ModelRegistry::size() const {
  std::lock_guard lk(mu_);
  return models_.size();
}

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

Result<ProviderManifest> ProviderManifest::from_json(const Json&) {
  return LOOM_NOT_IMPLEMENTED("ProviderManifest::from_json");  // STUB: wave2
}

bool constraints_satisfied(const Json&, const Json&) { return false; }  // STUB: wave2

ProviderRegistry::ProviderRegistry(const Config& cfg, const Secrets& secrets) : cfg_(cfg), secrets_(secrets) {}
Status ProviderRegistry::load_builtin() { return {}; }  // STUB: wave2 (no manifests yet)
Status ProviderRegistry::load(const Json&) { return LOOM_NOT_IMPLEMENTED("ProviderRegistry::load"); }  // STUB: wave2
bool ProviderRegistry::available(const ProviderManifest&) const { return false; }  // STUB: wave2
bool ProviderRegistry::can(std::string_view, std::string_view, const Json&) const { return false; }  // STUB: wave2
std::vector<ProviderManifest> ProviderRegistry::find(std::string_view, std::string_view, const Json&) const {
  return {};  // STUB: wave2
}
std::optional<ProviderManifest> ProviderRegistry::get(std::string_view) const { return std::nullopt; }  // STUB: wave2
std::vector<ProviderManifest> ProviderRegistry::all() const { return {}; }  // STUB: wave2
Json ProviderRegistry::status() const { return Json{{"providers", Json::array()}}; }  // STUB: wave2

}  // namespace loom
