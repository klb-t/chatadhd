// Which model a background reader (the guardian, the analyzer) may use: a policy as data. See loom/annotation_layer.h.
#include <algorithm>

#include "loom/annotation_layer.h"
#include "loom/config.h"
#include "loom/log.h"
#include "loom/port_profile.h"
#include "loom/scope_resolver.h"
#include "loom/util/utf8.h"

namespace loom::annotations {
namespace {

std::string param_model(const Json& resolved) {
  const Json model = resolved.value("params", Json::object()).value("model", Json());
  return model.is_string() ? model.get<std::string>() : std::string();
}

std::string lower(const std::string& s) { return utf8::to_lower(s); }

}  // namespace

std::string model_tier(const Json& models, const std::string& model, double price) {
  const Json tiers = models.value("tiers", Json::array());
  if (!tiers.is_array() || model.empty()) return "unknown";
  if (price >= 0) {
    for (const auto& t : tiers) {
      if (t.contains("max_per_million") && price <= json::get_number(t, "max_per_million", 0)) return json::get_string(t, "id");
      if (!t.contains("max_per_million") && !t.contains("patterns")) return json::get_string(t, "id");
    }
  }
  const std::string m = lower(model);
  for (const auto& t : tiers) {
    const Json pats = t.value("patterns", Json::array());
    for (const auto& p : pats)
      if (p.is_string() && m.find(lower(p.get<std::string>())) != std::string::npos) return json::get_string(t, "id");
  }
  return "unknown";
}

ModelChoice resolve_model(const Config& cfg, const std::filesystem::path& root, const std::string& policy, const std::string& conv_id,
                          const std::string& conversation_model, double price, Database* db) {
  ModelChoice out;
  out.via = "none";
  auto pol = Policy::load_cached(root);
  if (!pol) {
    out.reason = "the guardian profile could not be read";
    return out;
  }
  const Json models = pol->models;
  const Json chain = models.value("policies", Json::object()).value(policy, Json::object()).value("chain", Json::array());
  std::optional<PortProfile> ports;
  if (auto loaded = PortProfile::load(root)) ports = std::move(*loaded);
  auto role_model = [&](const std::string& role) {
    if (!ports) return std::string();
    auto r = ports->resolve(role, Json::object(), conv_id, cfg.all());
    return r ? param_model(*r) : std::string();
  };
  // A setting is read through the one scope chain (loom/scope_resolver.h): conversation -> units -> user -> installation, rules included.
  std::optional<scopes::Resolver> resolver;
  bool resolver_tried = false;
  auto setting_value = [&](const std::string& setting) -> Json {
    if (setting.empty()) return Json();
    if (!resolver_tried) {
      resolver_tried = true;
      if (auto opened = scopes::Resolver::open(root, &cfg, db)) resolver = std::move(*opened);
      else log::write(log::Level::Debug, "annotations", "scope chain not readable, settings decide: " + opened.error().message);
    }
    if (resolver) {
      scopes::Binding b;
      b.key = setting;
      b.conv_id = conv_id;
      if (auto r = resolver->resolve(b)) return r->defined ? r->value : Json();
      else log::write(log::Level::Debug, "annotations", "'" + setting + "' not resolved through the scope chain: " + r.error().message);
    }
    return cfg.get(setting);
  };
  auto tier_index = [&](const std::string& tier) {
    const Json tiers = models.value("tiers", Json::array());
    for (std::size_t i = 0; i < tiers.size(); ++i)
      if (json::get_string(tiers[i], "id") == tier) return static_cast<int>(i);
    return -1;
  };
  for (const auto& step : chain) {
    const std::string type = json::get_string(step, "type");
    std::string model, via;
    if (type == "none") break;
    if (type == "explicit") {
      if (step.contains("model")) model = json::get_string(step, "model");
      else {
        const Json v = setting_value(json::get_string(step, "setting"));
        if (v.is_string()) model = v.get<std::string>();
      }
      via = "explicit:" + json::get_string(step, "setting", "model");
    } else if (type == "role") {
      model = role_model(json::get_string(step, "role"));
      via = "role:" + json::get_string(step, "role");
    } else if (type == "relative_to") {
      // Derived from another model by the tier scale; only ever by a policy the user wrote.
      std::string base;
      const Json b = step.value("base", Json("conversation"));
      if (b.is_string() && b.get<std::string>() == "conversation") base = conversation_model;
      else if (b.is_object() && b.contains("role")) base = role_model(json::get_string(b, "role"));
      else if (b.is_object() && b.contains("setting")) {
        const Json v = setting_value(json::get_string(b, "setting"));
        if (v.is_string()) base = v.get<std::string>();
      }
      const int at = tier_index(model_tier(models, base, price));
      if (base.empty() || at < 0) {
        out.reason = "relative_to: the base model or its tier is unknown";
        continue;
      }
      const Json tiers = models.value("tiers", Json::array());
      const int target = std::clamp(at + static_cast<int>(json::get_int(step, "tier_offset", 0)), 0, static_cast<int>(tiers.size()) - 1);
      const Json mapped = models.value("tier_models", Json::object()).value(json::get_string(tiers[target], "id"), Json(""));
      model = mapped.is_string() ? mapped.get<std::string>() : std::string();
      via = "relative_to:" + json::get_string(tiers[target], "id");
      if (model.empty()) out.reason = "relative_to: no model is mapped to tier " + json::get_string(tiers[target], "id") + " (models.tier_models)";
    }
    if (!model.empty()) {
      out.model = model;
      out.via = via;
      out.reason.clear();
      out.tier = model_tier(models, model, price);
      out.flow = json::get_string(models.value("flows", Json::object()), out.tier, json::get_string(models.value("flows", Json::object()), "unknown"));
      return out;
    }
  }
  // The reason names the policy, so the reader of policy chat is not told about an analysis model.
  if (out.reason.empty())
    out.reason = chain.empty() ? "policy '" + policy + "' is not defined (guardian.pack models.policies)"
                               : "policy '" + policy + "': no step gave a model";
  return out;
}

}  // namespace loom::annotations
