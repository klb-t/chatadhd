#include "loom/settings_fields.h"

#include <set>

#include "loom/action_rules.h"
#include "loom/runtime_profile.h"

namespace loom {

namespace {
const Json* find_effect(const Json& effects, const std::string& id) {
  for (const auto& e : effects) if (e.value("id", "") == id) return &e;
  return nullptr;
}
}  // namespace

Result<Json> settings_fields(const std::filesystem::path& data_dir) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("settings_fields", data_dir));
  const Json& v = profile.values();
  // The edit rules are general rules over (target, action) in the one rule store (action_rules);
  // a field is an element of class `element_class`, "edit" the action, the installation the scope.
  LOOM_TRY_ASSIGN(auto engine, actions::Engine::load(data_dir));
  LOOM_TRY_ASSIGN(auto scope, engine.parse_scope(Json(v.value("rule_scope", ""))));
  const std::string rule_action = v.value("rule_action", "");
  Json fields = Json::array();
  const Json& cfg = config_defaults();
  const Json& lcfg = loom_config_defaults();
  for (const auto& f : v.at("fields")) {
    const std::string scope_name = f.value("scope", ""), key = f.value("key", "");
    Json o = f;
    o.erase("has_default");
    const bool has_default = f.value("has_default", false);
    o["default"] = has_default ? "present" : "absent";
    if (scope_name == "config" && has_default) {
      if (cfg.contains(key)) o["default_value"] = cfg.at(key);
      else if (lcfg.contains(key)) o["default_value"] = lcfg.at(key);
    }
    actions::Element element;
    element.cls = v.value("element_class", "");
    element.attrs = Json{{"field_scope", scope_name}, {"key", key}};
    LOOM_TRY_ASSIGN(auto decision, engine.decide(element, rule_action, scope));
    const std::string& effect = decision.effect;  // "allow" when no rule matches (R43)
    const Json* e = find_effect(v.at("effects"), effect);
    o["edit"] = e ? e->value("edit", effect) : effect;  // open string: unknown effects pass through
    o["rule"] = Json{{"action", rule_action}, {"effect", effect}};
    fields.push_back(std::move(o));
  }
  return Json{{"schema", "loom.settings_fields/1"}, {"edit_rules", v.at("effects")}, {"fields", fields}};
}

std::vector<std::string> settings_fields_problems(const Json& doc) {
  std::vector<std::string> out;
  std::set<std::string> described;
  for (const auto& f : doc.at("fields")) {
    const std::string scope = f.value("scope", ""), key = f.value("key", "");
    if (key.empty() || (scope != "config" && scope != "secret")) { out.push_back("bad field entry"); continue; }
    if (!described.insert(scope + ":" + key).second) out.push_back("duplicate " + scope + ":" + key);
    if (f.value("edit", "").empty()) out.push_back("no edit rule: " + key);
    if (f.value("description", "").empty()) out.push_back("no description: " + key);
    const bool has_default = scope == "config" && (config_defaults().contains(key) || loom_config_defaults().contains(key));
    const std::string declared = f.value("default", "");
    if (declared != "present" && declared != "absent") out.push_back("default must be present|absent: " + key);
    else if ((declared == "present") != has_default) out.push_back("default mismatch: " + key);
    if (has_default && !f.contains("default_value")) out.push_back("default value missing: " + key);
    if (scope == "secret" && (f.contains("default_value") || !f.value("sensitive", false)))
      out.push_back("secret must be sensitive without value: " + key);
  }
  for (const Json* d : {&config_defaults(), &loom_config_defaults()})
    for (auto it = d->begin(); it != d->end(); ++it)
      if (!described.count("config:" + it.key())) out.push_back("undescribed default: " + it.key());
  return out;
}

std::string settings_edit_rule(std::string_view scope, std::string_view key) {
  auto doc = settings_fields();
  if (!doc) return "";
  for (const auto& f : doc->at("fields"))
    if (f.value("scope", "") == scope && f.value("key", "") == key) return f.value("edit", "");
  return "";
}

Status check_config_write(std::string_view key, const std::filesystem::path& data_dir) {
  auto doc = settings_fields(data_dir);
  if (!doc) return {};  // metadata unavailable: never block writes
  for (const auto& f : doc->at("fields")) {
    if (f.value("scope", "") != "config" || f.value("key", "") != key) continue;
    // Only the effect "deny" (label read_only) is enforced by the generic write path.
    if (f.at("rule").value("effect", "") == "deny")
      return Error(Errc::InvalidArgument, "config key '" + std::string(key) + "' is read_only");
  }
  return {};
}

}  // namespace loom
