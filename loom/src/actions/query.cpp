// The questions asked of the engine: which actions fit an element (actions_for), why one does or does
// not (explain), and the dispatch behind loom_invoke port "actions".
#include <algorithm>

#include "loom/action_rules.h"

namespace loom::actions {

namespace {

std::vector<std::string> strings_of(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

Json decided_by(const Decision& d) {
  if (!d.rule.empty()) {
    Json o{{"basis", "rule"}, {"rule", d.rule}, {"effect", d.effect}, {"decision", d.decision}, {"initiator", d.initiator}};
    if (!d.conflict_rule.empty()) o["conflict_rule"] = d.conflict_rule;
    return o;
  }
  if (!d.conflict_rule.empty()) {  // the rules disagree and the conflict rule says to ask
    Json ids = Json::array();
    for (const auto& r : d.considered)
      if (json::get_string(r, "role") == "conflict") ids.push_back(json::get_string(r, "id"));
    return Json{{"basis", "conflict"}, {"conflict_rule", d.conflict_rule}, {"decision", d.decision}, {"rules", ids}};
  }
  return Json{{"basis", "no_rule"}, {"decision", d.decision}};
}

}  // namespace

Json Engine::element_json(const Element& element, const Prepared& p) const {
  Json traits = Json::array();
  for (const auto& [t, from] : p.traits) traits.push_back(t);  // std::map: sorted, deterministic
  Json groups = Json::array();
  for (auto it = groups_.begin(); it != groups_.end(); ++it)
    if (in_group(it.key(), p.traits)) groups.push_back(it.key());
  Json o = Json::object();
  if (!element.id.empty()) o["id"] = element.id;
  o["class"] = element.cls;
  o["class_known"] = p.class_known;
  o["traits"] = traits;
  o["groups"] = groups;
  o["attrs"] = element.attrs;
  return o;
}

Json Engine::row_for(const std::string& id, const Json& action, const Prepared& p, const Scope& scope,
                     const std::string& default_id) const {
  const Decision d = decide_prepared(p, id, scope);
  const bool is_default = id == default_id;
  std::string status = d.status;
  if (is_default && d.decision == "allow")
    if (const Json* dec = json::find(decisions_, "default_action")) status = json::get_string(*dec, "status", status);
  const std::string implementation = json::get_string(action, "status", "planned");
  Json matched = Json::array();
  const auto requires_ = strings_of(json::find(action, "requires"));
  for (const auto& t : requires_) {
    auto it = p.traits.find(t);
    matched.push_back(Json{{"trait", t}, {"from", it == p.traits.end() ? std::string() : it->second}});
  }
  Json row = Json::object();
  row["action"] = id;
  row["title"] = json::get_string(action, "title", id);
  row["kind"] = json::get_string(action, "kind");
  row["implementation"] = implementation;
  row["callable"] = implementation == "implemented";
  row["status"] = status;
  row["default"] = is_default;
  if (!d.reason.is_null()) row["reason"] = d.reason;
  if (!d.initiator.empty()) row["initiator"] = d.initiator;
  if (const Json* v = json::find(action, "effects")) row["effects"] = *v;
  row["content_op"] = json::get_string(action, "content_op", "none");
  row["form_op"] = json::get_string(action, "form_op", "none");
  row["cost"] = json::get_number(action, "cost", 0.0);
  row["confidence"] = json::get_number(action, "confidence", 1.0);
  Json groups = Json::array();
  for (const auto& g : groups_of_action(action)) groups.push_back(g);
  row["groups"] = groups;
  row["explanation"] = Json{{"requires", strings_of(json::find(action, "requires"))}, {"matched", matched}, {"decided_by", decided_by(d)}};
  return row;
}

Result<Json> Engine::actions_for(const Json& element_json_in, const Json& scope_in, const Json& options) const {
  LOOM_TRY_ASSIGN(Element element, parse_element(element_json_in));
  LOOM_TRY_ASSIGN(Scope scope, parse_scope(scope_in));
  const bool include_unmatched = json::get_bool(options, "include_unmatched", false);
  const bool implemented_only = json::get_bool(options, "implemented_only", false);
  const Prepared p = prepare(element);
  Json issues = Json::array();
  if (!element.cls.empty() && !p.class_known)
    issues.push_back(Json{{"code", "class_unknown"}, {"class", element.cls}});
  std::string default_id = default_action(p, scope);
  Json rows = Json::array();
  bool default_listed = false;
  for (auto it = actions_.begin(); it != actions_.end(); ++it) {
    const Json& action = it.value();
    if (implemented_only && json::get_string(action, "status") != "implemented") continue;
    std::vector<std::string> missing;
    for (const auto& t : strings_of(json::find(action, "requires")))
      if (!p.traits.count(t)) missing.push_back(t);
    if (!missing.empty()) {
      if (include_unmatched)
        rows.push_back(Json{{"action", it.key()}, {"title", json::get_string(action, "title", it.key())},
                            {"implementation", json::get_string(action, "status", "planned")},
                            {"status", "not_applicable"}, {"default", false}, {"missing", missing}});
      continue;
    }
    Json row = row_for(it.key(), action, p, scope, default_id);
    default_listed = default_listed || it.key() == default_id;
    rows.push_back(std::move(row));
  }
  if (!default_id.empty() && !default_listed) {  // named by a rule, but it does not fit (or is not in the catalogue)
    issues.push_back(Json{{"code", "default_action_not_fitting"}, {"action", default_id}});
    default_id.clear();
  }
  Json ordered = Json::array();  // the default action first, the rest in catalogue order
  for (const auto& r : rows) if (json::get_bool(r, "default", false)) ordered.push_back(r);
  for (const auto& r : rows) if (!json::get_bool(r, "default", false)) ordered.push_back(r);
  rows = std::move(ordered);
  Json out = Json::object();
  out["schema"] = "loom.actions_for/1";
  out["element"] = element_json(element, p);
  out["scope"] = scope_json(scope);
  out["default_action"] = default_id.empty() ? Json(nullptr) : Json(default_id);
  out["actions"] = rows;
  out["issues"] = issues;
  return out;
}

Result<Json> Engine::explain(const Json& element_json_in, std::string_view action_id, const Json& scope_in) const {
  LOOM_TRY_ASSIGN(Element element, parse_element(element_json_in));
  LOOM_TRY_ASSIGN(Scope scope, parse_scope(scope_in));
  const Json* action = json::find(actions_, action_id);
  if (!action) return Error(Errc::NotFound, "unknown action '" + std::string(action_id) + "'");
  const Prepared p = prepare(element);
  const std::string id(action_id);
  Json requires_ = Json::array();
  Json missing = Json::array();
  for (const auto& t : strings_of(json::find(*action, "requires"))) {
    auto it = p.traits.find(t);
    if (it == p.traits.end()) {
      requires_.push_back(Json{{"trait", t}, {"met", false}});
      missing.push_back(t);
    } else {
      requires_.push_back(Json{{"trait", t}, {"met", true}, {"from", it->second}});
    }
  }
  const std::string default_id = default_action(p, scope);
  const Decision d = decide_prepared(p, id, scope);
  const bool fits = missing.empty();
  std::string status = "not_applicable";
  if (fits) {
    status = d.status;
    if (id == default_id && d.decision == "allow")
      if (const Json* dec = json::find(decisions_, "default_action")) status = json::get_string(*dec, "status", status);
  }
  Json summary = Json::object();
  for (const char* k : {"title", "kind", "status", "content_op", "form_op", "cost", "confidence", "effects", "produces"})
    if (const Json* v = json::find(*action, k)) summary[k == std::string("status") ? "implementation" : k] = *v;
  summary["id"] = id;
  Json groups = Json::array();
  for (const auto& g : groups_of_action(*action)) groups.push_back(g);
  summary["groups"] = groups;
  Json out = Json::object();
  out["schema"] = "loom.actions_explain/1";
  out["element"] = element_json(element, p);
  out["scope"] = scope_json(scope);
  out["action"] = summary;
  out["fits"] = fits;
  out["status"] = status;
  out["default"] = id == default_id;
  out["requires"] = requires_;
  out["missing"] = missing;
  if (!d.reason.is_null()) out["reason"] = d.reason;
  if (!d.initiator.empty()) out["initiator"] = d.initiator;
  out["decided_by"] = decided_by(d);
  out["rules"] = d.considered;
  if (!fits) {  // the missing traits may be within reach: the chains that would supply them
    auto proposal = chains(p, Json{{"action", id}}, scope, Json::object());
    if (proposal) out["extrapolation"] = *proposal;
  }
  return out;
}

Result<Json> Engine::propose_chain(const Json& element_json_in, const Json& target, const Json& scope_in,
                                   const Json& options) const {
  LOOM_TRY_ASSIGN(Element element, parse_element(element_json_in));
  LOOM_TRY_ASSIGN(Scope scope, parse_scope(scope_in));
  return chains(prepare(element), target, scope, options);
}

// ── loom_invoke port "actions" ─────────────────────────────────────

Result<Json> invoke(const std::filesystem::path& data_dir, std::string_view action, const Json& request) {
  if (!request.is_object()) return Error(Errc::InvalidArgument, "request must be a JSON object");
  const Json* element = json::find(request, "element");
  if (!element) return Error(Errc::InvalidArgument, "element is required");
  LOOM_TRY_ASSIGN(auto engine, Engine::load(data_dir));
  const Json* scope = json::find(request, "scope");
  const Json no_scope;
  if (action == "for") {
    Json options = Json::object();
    for (const char* k : {"include_unmatched", "implemented_only"})
      if (const Json* v = json::find(request, k)) options[k] = *v;
    return engine.actions_for(*element, scope ? *scope : no_scope, options);
  }
  if (action == "explain") {
    const std::string id = json::get_string(request, "action");
    if (id.empty()) return Error(Errc::InvalidArgument, "action is required");
    return engine.explain(*element, id, scope ? *scope : no_scope);
  }
  if (action == "propose_chain") {
    const std::string trait = json::get_string(request, "target_trait");
    const std::string target_action = json::get_string(request, "target_action");
    if (trait.empty() == target_action.empty())
      return Error(Errc::InvalidArgument, "name exactly one of target_trait and target_action");
    Json options = Json::object();
    for (const char* k : {"preset", "max_length", "max_cost", "min_confidence", "max_results"})
      if (const Json* v = json::find(request, k)) options[k] = *v;
    const Json target = trait.empty() ? Json{{"action", target_action}} : Json{{"trait", trait}};
    return engine.propose_chain(*element, target, scope ? *scope : no_scope, options);
  }
  return Error(Errc::InvalidArgument, "unknown actions action '" + std::string(action) + "'");
}

}  // namespace loom::actions
